#include "event_loop.h"
#include <fcntl.h>
#include <string.h>
#include <map>
#include "tcp_conn.h"
#include "logging.h"
#include "poller.h"
#include "util.h"
#include "channel.h"

using namespace std;

namespace titan {

struct IdleImp;

EventLoop::EventLoop(int taskCap)
        : poller_(new PollerEpoll()), exit_(false), nextTimeout_(1 << 30), tasks_(taskCap), timerSeq_(0), idleEnabled(false) {
    int r = pipe2(wakeupFds_, O_CLOEXEC);
    fatalif(r, "pipe2 failed %d %s", errno, strerror(errno));
    trace("wakeup pipe created %d %d", wakeupFds_[0], wakeupFds_[1]);
    Channel *ch = new Channel(this, wakeupFds_[0], kReadEvent);
    ch->setReadCallback([=] {
        char buf[1024];
        int r = ch->fd() >= 0 ? ::read(ch->fd(), buf, sizeof buf) : 0;
        if (r > 0) {
            Task task;
            while (tasks_.pop_wait(&task, 0)) {
                task();
            }
        } else if (r == 0) { // Channel::close() => handleRead()
            trace("delete wakeup channel");
            delete ch;
        } else if (errno == EINTR) {
        } else {
            fatal("wakeup channel read error %d %d %s", r, errno, strerror(errno));
        }
    });
}

EventLoop::~EventLoop() {
    delete poller_;  
    ::close(wakeupFds_[1]);
}

void EventLoop::loop() {
    while (!exit_) {
        loop_once(10000); // 最长等待时间是10s
    }
    timers_.clear();
    idleConns_.clear();
    for (auto recon : reconnectConns_) {  //重连的连接无法通过channel清理，因此单独清理
        recon->cleanup(recon);
    }
    loop_once(0);
}

void EventLoop::loop_once(int waitMs) {
    poller_->loop_once(std::min(waitMs, nextTimeout_));
    handleTimeouts();
}

IdleId EventLoop::registerIdle(int idle, const TcpConnPtr &con, const TcpCallback &cb) { // 注册一个空闲连接
    if (!idleEnabled) {
        runAfter(1000, [this] { callIdles(); }, 1000); // 注册一个定时器, 该定时器任务每秒都要执行一次callIdles().
        idleEnabled = true;
    }
    auto &lst = idleConns_[idle];
    lst.push_back(IdleNode{con, util::timeMilli() / 1000, move(cb)});
    trace("register idle");
    return IdleId(new IdleIdImp(&lst, --lst.end())); // 使用IdleNode所在链表和指向它的迭代器来表示它.
}

void EventLoop::unregisterIdle(const IdleId &id) { // 删除一个空闲连接
    trace("unregister idle");
    id->lst_->erase(id->iter_);
}

void EventLoop::updateIdle(const IdleId &id) {
    trace("update idle");
    id->iter_->updated_ = util::timeMilli() / 1000;
    id->lst_->splice(id->lst_->end(), *id->lst_, id->iter_);
}

void EventLoop::callIdles() { // idleConns_按照寿命由小到大排序, 寿命相同的连接链表按照updated从小到大排序
    int64_t now = util::timeMilli() / 1000;
    for (auto &l : idleConns_) {
        int idle = l.first;
        auto lst = l.second;
        while (lst.size()) {
            IdleNode &node = lst.front();
            if (node.updated_ + idle > now) {
                break;
            }
            node.updated_ = now;
            lst.splice(lst.end(), lst, lst.begin());
            node.cb_(node.con_);
        }
    }
}

TimerId EventLoop::runAt(int64_t milli, Task &&task, int64_t interval) { // 添加定时任务: 一次性任务和重复任务
    if (exit_) {
        return TimerId();
    }

    TimerId tid{milli, ++timerSeq_};
    if (interval) {
        Task repeatableTask = std::bind(
                [this, milli, interval] (Task &task) { onRepeatableTimer(milli, interval, std::move(task)); },
                std::move(task)
            );
        timers_.insert({tid, repeatableTask});
    } else {
        timers_.insert({tid, std::move(task)});
    }

    updateNextTimeOut();  // handleTimeOuts()之后和下一次loop()之间添加定时器时, 需要更新nextTimeOut_.
    return tid;
}

bool EventLoop::cancel(TimerId timerid) {
    auto p = timers_.find(timerid);
    if (p != timers_.end()) {
        timers_.erase(p);
        return true;
    }
    return false;
}

void EventLoop::handleTimeouts() { //getExpired: 同步执行已到期的Timer, 并在timers_移除它们...
    int64_t now = util::timeMilli();
    TimerId tid{now, 1L << 62};
    while (timers_.size() && timers_.begin()->first < tid) {
        Task task = move(timers_.begin()->second);
        timers_.erase(timers_.begin()); // std::map.erase O(log(n))
        task();
    }
    updateNextTimeOut();
}

void EventLoop::updateNextTimeOut() { // 更新距离当前时刻最近的定时器的超时时间间隔
    if (timers_.empty()) {
        nextTimeout_ = 1 << 30; // 大概是20多天时间
    } else {
        const TimerId &t = timers_.begin()->first;
        nextTimeout_ = t.first - util::timeMilli();
        nextTimeout_ = nextTimeout_ < 0 ? 0 : nextTimeout_;
    }
}

// 重复性定时器到期时执行的task.
void EventLoop::onRepeatableTimer(int64_t milli, int64_t interval, Task &&task) { // 更新并添加重复任务到timers_, 然后执行一次该任务
    int64_t nextAt = milli + interval;
    TimerId tid{nextAt, ++timerSeq_};
    Task repeatableTask = std::bind(
            [this, nextAt, interval] (Task &task) { onRepeatableTimer(nextAt, interval, std::move(task)); },
            std::move(task)
        );
    timers_.insert({tid, repeatableTask});
    task();
}

void EventLoop::safeCall(Task &&task) { // 跨线程添加计算任务. void addTask(Task &&task)
    tasks_.push(std::move(task));
    wakeup(); // IO线程唤醒之后, 就会执行tasks_中的任务
}

void EventLoopThreadPool::loop() {
    int sz = loops_.size();
    for (int i = 0; i < sz - 1; i++) {
        threads_[i] = std::thread([this, i] { loops_[i].loop(); }); // explicit thread( Function&& f, Args&&... args );
    }
    loops_.back().loop(); // main thread
    for (int i = 0; i < sz - 1; i++) {
        threads_[i].join();
    }
}

void titanUnregisterIdle(EventLoop *loop, const IdleId &idle) {
    loop->unregisterIdle(idle);
}

void titanUpdateIdle(EventLoop *loop, const IdleId &idle) {
    loop->updateIdle(idle);
}

}  // namespace titan