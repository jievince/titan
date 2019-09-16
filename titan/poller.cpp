#include <sys/epoll.h>
#include <fcntl.h>
#include "tcp_conn.h"
#include "event_loop.h"
#include "logging.h"
#include "util.h"
#include "poller.h"

namespace titan {

EpollPoller::EpollPoller() : lastActive_(-1) {
    static std::atomic<int64_t> id(0);
    id_ = id++;
    epfd_ = epoll_create1(EPOLL_CLOEXEC);
    fatalif(epfd_ < 0, "epoll_create error %d %s", errno, strerror(errno));
    info("poller epoll %d created", epfd_);
}

EpollPoller::~EpollPoller() { // 销毁poller的同时会销毁掉poller所关注的channel
    info("destroying poller %d", epfd_);
    while (liveChannels_.size()) {
        (*liveChannels_.begin())->close(); // Channel::close();
    }
    ::close(epfd_);
    info("poller %d destroyed", epfd_);
}

void EpollPoller::addChannel(Channel *ch) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = ch->events();
    ev.data.ptr = ch;
    trace("adding channel %lld fd %d events %d epoll %d", (long long) ch->id(), ch->fd(), ev.events, epfd_);
    int r = epoll_ctl(epfd_, EPOLL_CTL_ADD, ch->fd(), &ev);
    fatalif(r, "epoll_ctl add failed %d %s", errno, strerror(errno));
    liveChannels_.insert(ch);
}

void EpollPoller::updateChannel(Channel *ch) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = ch->events();
    ev.data.ptr = ch;
    trace("modifying channel %lld fd %d events read %d write %d epoll %d", (long long) ch->id(), ch->fd(), ev.events & EPOLLIN, ev.events & EPOLLOUT, epfd_);
    int r = epoll_ctl(epfd_, EPOLL_CTL_MOD, ch->fd(), &ev);
    fatalif(r, "epoll_ctl mod failed %d %s", errno, strerror(errno));
}

/* Close a file descriptor(!all fd refers to the same open file discription is closed) 
    will automatically removed it from an epoll set
*/
void EpollPoller::removeChannel(Channel *ch) {
    trace("removing channel %lld fd %d epoll %d", (long long) ch->id(), ch->fd(), epfd_);
    liveChannels_.erase(ch); // 删除ch指针
    for (int i = lastActive_ - 1; i >= 0; i--) {
        if (ch == activeEvs_[i].data.ptr) {
            activeEvs_[i].data.ptr = NULL;
            break;
        }
    }
}

void EpollPoller::loop_once(int waitMs) {
    int64_t ticks = util::timeMilli();
    lastActive_ = epoll_wait(epfd_, activeEvs_, kMaxEvents, waitMs);
    int64_t used = util::timeMilli() - ticks;
    trace("epoll wait %d return %d errno %d(%s) used %lld millsecond", waitMs, lastActive_, errno, strerror(errno), (long long) used);
    fatalif(lastActive_ == -1 && errno != EINTR, "epoll return error %d %s", errno, strerror(errno));
    while (--lastActive_ >= 0) {
        int i = lastActive_;
        Channel *ch = (Channel *) activeEvs_[i].data.ptr;
        int events = activeEvs_[i].events;
        if (ch) { // 若在epoll_wait返回后, removeChannel(ch), 则此时ch==NULL
            if (events & kWriteEvent) {
                trace("channel %lld fd %d handle write", (long long) ch->id(), ch->fd());
                ch->handleWrite();
            }
            if (events & kReadEvent) {
                trace("channel %lld fd %d handle read", (long long) ch->id(), ch->fd());
                ch->handleRead();
            }
            if (!(events & (kReadEvent | kWriteEvent))){
                fatal("unexpected poller events");
            }
        }
    }
}
}  // namespace titan