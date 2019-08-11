#include "channel.h"
#include "poller.h"
#include "event_loop.h"

using namespace std;

namespace titan {

Channel::Channel(EventLoop *loop, int fd, int events) : loop_(loop), fd_(fd), events_(events) {
    fatalif(net::setNonBlock(fd_) < 0, "channel set non block failed");
    static atomic<int64_t> id(0);
    id_ = ++id;
    loop_->poller_->addChannel(this);
}

Channel::~Channel() {
    close();
}

void Channel::enableRead(bool enable) {
    if (enable) {
        events_ |= kReadEvent;
    } else {
        events_ &= ~kReadEvent;
    }
    loop_->poller_->updateChannel(this);
}

void Channel::enableWrite(bool enable) {
    if (enable) {
        events_ |= kWriteEvent;
    } else {
        events_ &= ~kWriteEvent;
    }
    loop_->poller_->updateChannel(this);
}

void Channel::enableReadWrite(bool readable, bool writable) {
    if (readable) {
        events_ |= kReadEvent;
    } else {
        events_ &= ~kReadEvent;
    }
    if (writable) {
        events_ |= kWriteEvent;
    } else {
        events_ &= ~kWriteEvent;
    }
    loop_->poller_->updateChannel(this);
}

void Channel::close() { // poller并不拥有channel, channel在析构之前必须自己从poller中ungister(removeChannel), 避免造成空悬指针
    if (fd_ >= 0) {
        trace("close channel %ld fd %d", (long) id_, fd_);
        loop_->poller_->removeChannel(this);
        ::close(fd_);
        fd_ = -1;
        handleRead();
    }
}

bool Channel::readEnabled() {
    return events_ & kReadEvent;
}
bool Channel::writeEnabled() {
    return events_ & kWriteEvent;
}

}  // namespace titan