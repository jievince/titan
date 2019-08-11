#pragma once
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <atomic>
#include <map>

namespace titan {

const int kMaxEvents = 2000;
const int kReadEvent = EPOLLIN;
const int kWriteEvent = EPOLLOUT;

// poller class是IO multiplexing的封装
// 每个EventLoop都有自己的poller, 用id_标识它.
struct PollerEpoll : private noncopyable {
    PollerEpoll();
    ~PollerEpoll();
    void addChannel(Channel *ch);
    void removeChannel(Channel *ch);
    void updateChannel(Channel *ch);
    // 从poll返回到再次调用poll称为一次事件循环
    void loop_once(int waitMs);

    int64_t id_;
    int lastActive_;
    int epfd_; // epoll fd
    std::set<Channel *> liveChannels_; // poller所关心的channel列表
    // for epoll selected active events
    struct epoll_event activeEvs_[kMaxEvents];
};

}  // namespace titan