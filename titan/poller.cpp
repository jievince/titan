#include <sys/epoll.h>
#include <fcntl.h>
#include "conn.h"
#include "event_loop.h"
#include "logging.h"
#include "util.h"
#include "poller.h"

namespace titan {

PollerEpoll::PollerEpoll() : lastActive_(-1) {
    static std::atomic<int64_t> id(0);
    id_ = ++id;
    epfd_ = epoll_create1(EPOLL_CLOEXEC);
    fatalif(epfd_ < 0, "epoll_create error %d %s", errno, strerror(errno));
    info("poller epoll %d created", epfd_);
}

PollerEpoll::~PollerEpoll() { // 销毁poller的同时会销毁掉poller所关注的channel
    info("destroying poller %d", epfd_);
    while (liveChannels_.size()) {
        (*liveChannels_.begin())->close();
    }
    ::close(epfd_);
    info("poller %d destroyed", epfd_);
}

void PollerEpoll::addChannel(Channel *ch) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = ch->events();
    ev.data.ptr = ch;
    trace("adding channel %lld fd %d events %d epoll %d", (long long) ch->id(), ch->fd(), ev.events, epfd_);
    int r = epoll_ctl(epfd_, EPOLL_CTL_ADD, ch->fd(), &ev);
    fatalif(r, "epoll_ctl add failed %d %s", errno, strerror(errno));
    liveChannels_.insert(ch);
}

void PollerEpoll::updateChannel(Channel *ch) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = ch->events();
    ev.data.ptr = ch;
    trace("modifying channel %lld fd %d events read %d write %d epoll %d", (long long) ch->id(), ch->fd(), ev.events & EPOLLIN, ev.events & EPOLLOUT, epfd_);
    int r = epoll_ctl(epfd_, EPOLL_CTL_MOD, ch->fd(), &ev);
    fatalif(r, "epoll_ctl mod failed %d %s", errno, strerror(errno));
}

void PollerEpoll::removeChannel(Channel *ch) {
    trace("deleting channel %lld fd %d epoll %d", (long long) ch->id(), ch->fd(), epfd_);
    liveChannels_.erase(ch); // 删除ch指针
    /* A file descriptor is removed from an epoll set only after all the file descriptors 
    referring to the underlying open file description have been closed 
    (or before if the descriptor is explicitly removed using epoll_ctl(2) EPOLL_CTL_DEL). */
    // removeChannel是可以被跨线程调用的, 所以removeChannel有可能发生在epoll_wait返回后到遍历到该channel之间
    for (int i = lastActive_ - 1; i >= 0; i--) {
        if (ch == activeEvs_[i].data.ptr) {
            activeEvs_[i].data.ptr = NULL;
            break;
        }
    }
}

void PollerEpoll::loop_once(int waitMs) {
    int64_t ticks = util::timeMilli();
    lastActive_ = epoll_wait(epfd_, activeEvs_, kMaxEvents, waitMs);
    int64_t used = util::timeMilli() - ticks;
    trace("epoll wait %d return %d errno %d used %lld millsecond", waitMs, lastActive_, errno, (long long) used);
    fatalif(lastActive_ == -1 && errno != EINTR, "epoll return error %d %s", errno, strerror(errno));
    while (--lastActive_ >= 0) {
        int i = lastActive_;
        Channel *ch = (Channel *) activeEvs_[i].data.ptr;
        int events = activeEvs_[i].events;
        if (ch) { // 若之前removeChannel(ch), 则ch==NULL
            if (events & kWriteEvent) {
                trace("channel %lld fd %d handle write", (long long) ch->id(), ch->fd());
                ch->handleWrite();
            }
            if (events & (kReadEvent | EPOLLERR)) {
                trace("channel %lld fd %d handle read", (long long) ch->id(), ch->fd());
                ch->handleRead();
            }
            if (!(events & (kReadEvent | kWriteEvent | EPOLLERR))){
                fatal("unexpected poller events");
            }
        }
    }
}
}  // namespace titan