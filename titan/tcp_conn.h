#pragma once
#include "event_loop.h"
#include "channel.h"

namespace titan {

struct AutoContext : noncopyable {
    void *ctx;
    Task ctxDel;
    AutoContext() : ctx(0) {}
    template <class T>
    T &context() {
        if (ctx == NULL) {
            ctx = new T();
            ctxDel = [this] { delete (T *) ctx; };
        }
        return *(T *) ctx;
    }
    ~AutoContext() {
        if (ctx)
            ctxDel();
    }
};

// Tcp连接，使用引用计数
struct TcpConn : public std::enable_shared_from_this<TcpConn>, private noncopyable {
    // Tcp连接的5个状态
    enum State {
        Invalid = 1,
        Handshaking,
        Connected,
        Closed,
        Failed
    };
    // Tcp构造函数, 实际可用的连接应当通过createConnection创建
    TcpConn();
    ~TcpConn();

    // 供给客户端用
    static TcpConnPtr createConnection(EventLoop *loop, const std::string &host, unsigned short port, int timeout = 0, const std::string &localip = "") {
        TcpConnPtr con(new TcpConn); // default constructor
        info("creating new connection connecting to host %s port %d", host.c_str(), port);
        con->connect(loop, host, port, timeout, localip); // state_ is State::Invalid
        return con;
    }

    // 供给客户端用
    static TcpConnPtr createConnection(EventLoop *loop, int fd, Ip4Addr local, Ip4Addr peer) {
        TcpConnPtr con(new TcpConn);
        con->attach(loop, fd, local, peer);
        return con;
    }

    bool isClient() { return isClient_; }
    // automatically managed context. allocated when first used, deleted when destruct
    template <class T>
    T &context() {
        return ctx_.context<T>();
    }

    EventLoop *getLoop() { return loop_; }
    State getState() { return state_; }
    // TcpConn的输入输出缓冲区
    Buffer &getInput() { return input_; }
    Buffer &getOutput() { return output_; }                   

    Channel *getChannel() { return channel_; }
    bool writable() { return channel_ ? channel_->writeEnabled() : false; }

    //发送数据
    void sendOutput() { send(output_); }
    void send(Buffer &msg);
    void send(const char *buf, size_t len);
    void send(const std::string &s) { send(s.data(), s.size()); }
    void send(const char *s) { send(s, strlen(s)); }

    // tcp空闲回调
    void addIdleCB(int idle, const TcpCallback &cb);

    //数据到达时回调
    void setReadCallback(const TcpCallback &cb) {
        assert(!readcb_);
        readcb_ = cb;
    };
    //当tcp缓冲区可写时回调
    void setWriteCallback(const TcpCallback &cb) { writablecb_ = cb; }
    // tcp状态改变时回调
    void setStateCallback(const TcpCallback &cb) { statecb_ = cb; }
    //消息回调，此回调与setReadCallback回调冲突，只能够调用一个; codec所有权交给setMsgCallback
    void setMsgCallback(CodecBase *codec, const MsgCallback &cb);
    
    //发送消息
    void sendMsg(Slice msg);

    // conn会在下个事件周期进行处理
    void close();
    //设置重连时间间隔，-1: 不重连，0:立即重连，其它：等待毫秒数，未设置不重连
    void setReconnectInterval(int milli) { reconnectInterval_ = milli; }

    //!慎用. 立即关闭连接，清理相关资源，可能导致该连接的引用计数变为0，从而使当前调用者引用的连接被析构
    void closeNow() {
        if (channel_)
            channel_->close();
    }

    //远程地址的字符串
    std::string peerAddrStr() { return peer_.toString(); }

   public:
    EventLoop *loop_;
    Channel *channel_; // 管理本连接的cfd
    Buffer input_, output_; // 应用层输入输出缓冲区
    Ip4Addr local_, peer_;
    State state_;
    TcpCallback readcb_, writablecb_, statecb_;
    std::list<IdleId> idleIds_;
    TimerId timeoutId_;
    AutoContext ctx_, internalCtx_;
    std::string localIp_;
    std::string destHost_;
    unsigned short destPort_;
    bool isClient_;
    int connectTimeout_, reconnectInterval_;
    int64_t connectedTime_;
    std::unique_ptr<CodecBase> codec_;
    void handleRead(const TcpConnPtr &con);
    void handleWrite(const TcpConnPtr &con);
    ssize_t isend(const char *buf, size_t len);
    void cleanup(const TcpConnPtr &con);
    void attach(EventLoop *loop, int fd, Ip4Addr local, Ip4Addr peer);
    void connect(EventLoop *loop, const std::string &host, unsigned short port, int timeout, const std::string &localip);
    void reconnect();
    virtual int readImp(int fd, void *buf, size_t bytes) { return ::read(fd, buf, bytes); }
    virtual int writeImp(int fd, const void *buf, size_t bytes) { return ::write(fd, buf, bytes); }
    virtual int handleHandshake(const TcpConnPtr &con);
};

}  // namespace titan
