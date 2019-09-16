#pragma once
#include <list>
#include "titan-imp.h"
#include "poller.h"

namespace titan {

typedef std::shared_ptr<TcpConn> TcpConnPtr;
typedef std::shared_ptr<TcpServer> TcpServerPtr;
typedef std::function<void(const TcpConnPtr &)> TcpCallback;
typedef std::function<void(const TcpConnPtr &, Slice msg)> MsgCallback;

struct IdleNode {
    TcpConnPtr con_;
    int64_t updated_;
    TcpCallback cb_;
};

struct IdleIdImp {
    IdleIdImp() {}
    typedef std::list<IdleNode>::iterator Iter;
    IdleIdImp(std::list<IdleNode> *lst, Iter iter) : lst_(lst), iter_(iter) {}
    std::list<IdleNode> *lst_;
    Iter iter_;
};

struct TimerRepeatable {
    int64_t at;  // current timer timeout timestamp
    int64_t interval;
    TimerId timerid; // 此timerId就是timers_中的key
    Task cb;
};

struct EventLoopBases : private noncopyable {
    virtual EventLoop *allocEventLoop() = 0;
};

//事件派发器，可管理定时器，连接，超时连接
struct EventLoop : public EventLoopBases {
    EventLoop(int taskCap = 0);
    ~EventLoop();
    //进入事件处理循环
    void loop();
    //处理已到期的事件, waitMs表示若无当前需要处理的任务, 需要等待的时间
    void loop_once(int waitMs);

    IdleId registerIdle(int idle, const TcpConnPtr &con, const TcpCallback &cb);
    void unregisterIdle(const IdleId &id);
    void updateIdle(const IdleId &id);
    void callIdles();

    // 添加定时任务，interval=0表示一次性任务，否则为重复任务，时间为毫秒
    TimerId runAt(int64_t milli, Task &&task, int64_t interval = 0);
    TimerId runAt(int64_t milli, const Task &task, int64_t interval = 0) { return runAt(milli, Task(task), interval); }
    TimerId runAfter(int64_t milli, const Task &task, int64_t interval = 0) { return runAt(util::timeMilli() + milli, Task(task), interval); }
    TimerId runAfter(int64_t milli, Task &&task, int64_t interval = 0) { return runAt(util::timeMilli() + milli, std::move(task), interval); }
    // 取消定时任务
    bool cancel(TimerId timerid);
    void handleTimeouts(); // 处理超时定时器
    void updateNextTimeOut();
    void onRepeatableTimer(TimerRepeatable *tr);

    // 下列函数为线程安全的
    void exit() {
        exit_ = true;
        wakeup(); // 如果是IO线程调用exit()需要唤醒吗
    }
    bool exited() { return exit_; }
    //添加任务
    void safeCall(Task &&task);
    void safeCall(const Task &task) { safeCall(Task(task)); }
    void wakeup() {
        int r = write(wakeupFds_[1], "", 1);
        fatalif(r <= 0, "write error wd %d %d %s", r, errno, strerror(errno));
    }
    //分配一个事件派发器
    virtual EventLoop *allocEventLoop() { return this; }

    EpollPoller *poller_;
    std::atomic<bool> exit_; // exit_是是否退出事件处理循环loop()的标志
    int wakeupFds_[2];
    int nextTimeout_; // 即将生效的定时器发生时间与当前时间的差值
    SafeQueue<Task> tasks_; // task中的任务是在IO线程被wakeup()后, 执行的回调函数readcb_中执行的.
    std::map<TimerId, Task> timers_; // 定时器队列: 定时器包含一次性和重复性定时器
    std::map<TimerId, TimerRepeatable> timerReps_; // 重复任务队列
    std::atomic<int64_t> timerSeq_; // 定时器序号
    // 记录每个idle时间（单位秒）下所有的连接. 链表中的所有连接，最新的插入到链表末尾. 连接若有活动，会把连接从链表中移到链表尾部，做法参考memcache
    std::map<int, std::list<IdleNode>> idleConns_;
    std::set<TcpConnPtr> reconnectConns_;
    bool idleEnabled;
};

//多线程的事件派发器
struct MultiEventLoops : public EventLoopBases {
    MultiEventLoops(int sz) : id_(0), loops_(sz), threads_(sz-1) {} 
    virtual EventLoop *allocEventLoop() { // 使用round-robin算法分配EventLoop
        int c = id_++;
        return &loops_[c % loops_.size()];
    }
    void loop();
    MultiEventLoops &exit() {
        for (auto &b : loops_) {
            b.exit();
        }
        return *this;
    }

   private:
    std::atomic<int> id_;
    std::vector<EventLoop> loops_;
    std::vector<std::thread> threads_;
};

}  // namespace titan
