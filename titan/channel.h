#pragma once
#include "titan-imp.h"
#include "poller.h"

namespace titan {

/* 通道，封装了可以进行epoll的一个fd
  每个channel对象自始至终只属于一个EventLoop, 而one Event loop(one event base) one thread, 所以channel只属于一个IO线程. 
  每个channel对象自始至终只负责一个文件描述符fd的IO事件分发(不管理fd的生命周期).
  channel会把不同的IO事件分发为不同的回调, 如readcb_, writecb_.
  因为channel自始至终只会属于一个IO线程, channel的成员函数只能在IO线程中调用, 因此更新数据成员不必加锁
*/
struct Channel : private noncopyable {
    // base为事件管理器，fd为通道内部的fd，events为通道关心的事件
    Channel(EventLoop *loop, int fd, int events);
    ~Channel();
    EventLoop *getLoop() { return loop_; }
    int fd() { return fd_; }
    //通道id
    int64_t id() { return id_; }
    short events() { return events_; }
    //关闭通道
    void close();

    //挂接事件处理器
    void setReadCallback(const Task &readcb) { readcb_ = readcb; }
    void setWriteCallback(const Task &writecb) { writecb_ = writecb; }
    void setReadCallback(Task &&readcb) { readcb_ = std::move(readcb); }
    void setWriteCallback(Task &&writecb) { writecb_ = std::move(writecb); }

    //启用读写监听
    void enableRead(bool enable);
    void enableWrite(bool enable);
    void enableReadWrite(bool readable, bool writable);
    bool readEnabled();
    bool writeEnabled();

    //处理读写事件
    void handleRead() { readcb_(); }
    void handleWrite() { writecb_(); }

   protected:
    EventLoop *loop_;
    int fd_;
    short events_;
    int64_t id_;
    std::function<void()> readcb_, writecb_, errorcb_;
};

}  // namespace titan