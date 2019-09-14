#pragma once
#include "event_loop.h"
#include "channel.h"

namespace titan {

// Tcp服务器
// 单线程的TcpServer的EventLoop是与所有TcpConn所共享的; 
// 多线程的TcpServer自己的EventLoop只用来接受新连接, 而新连接会用其他EventLoop来执行IO.
struct TcpServer : private noncopyable { // TcpServer融合了acceptor
    TcpServer(EventLoopBases *bases);
    ~TcpServer() { delete listen_channel_; }
    // return 0 on sucess, errno on error
    int bind(const std::string &host, unsigned short port, bool reusePort = false);
    static TcpServerPtr startServer(EventLoopBases *bases, const std::string &host, unsigned short port, bool reusePort = false);
    Ip4Addr getAddr() { return addr_; }
    EventLoop *getLoop() { return loop_; }
    void setTcpConnCreateCallback(const std::function<TcpConnPtr()> &cb) { createcb_ = cb; }
    void setTcpConnStateCallback(const TcpCallback &cb) { statecb_ = cb; }
    void setTcpConnReadCallback(const TcpCallback &cb) { assert(!readcb_); readcb_ = cb;}
    void setTcpConnMsgCallback(CodecBase *codec, const MsgCallback &cb); // 消息处理与setTcpConnReadCallback回调冲突，只能调用一个

   private:
    EventLoop *loop_;
    EventLoopBases *bases_; // EventLoop or EventLoopThreadPool
    Ip4Addr addr_;
    Channel *listen_channel_;
    std::function<TcpConnPtr()> createcb_; // 创建tcp连接时的callback
    TcpCallback statecb_, readcb_;
    std::unique_ptr<CodecBase> codec_;
    void handleAccept();
    void addNewConn(EventLoop *newLoop, int fd, Ip4Addr local, Ip4Addr peer);  // 为新的cfd关联一个TcpConn对象
};

} // namespace titan