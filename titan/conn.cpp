#include "conn.h"
#include <fcntl.h>
#include <poll.h>
#include "logging.h"
#include "poller.h"
#include "channel.h"

using namespace std;
namespace titan {

void titanUnregisterIdle(EventLoop *loop, const IdleId &idle);
void titanUpdateIdle(EventLoop *loop, const IdleId &idle);

void TcpConn::attach(EventLoop *loop, int fd, Ip4Addr local, Ip4Addr peer) {
    fatalif((!isClient_ && state_ != State::Invalid) || (isClient_ && state_ != State::Handshaking),
            "you should use a new TcpConn to attach. state: %d", state_);
    loop_ = loop;
    state_ = State::Handshaking;
    local_ = local;
    peer_ = peer;
    delete channel_;
    channel_ = new Channel(loop, fd, kWriteEvent | kReadEvent);
    trace("tcp constructed %s - %s fd: %d", local_.toString().c_str(), peer_.toString().c_str(), fd);
    TcpConnPtr con = shared_from_this();
    con->channel_->setReadCallback([=] { con->handleRead(con); });
    con->channel_->setWriteCallback([=] { con->handleWrite(con); });
}

void TcpConn::connect(EventLoop *loop, const string &host, unsigned short port, int timeout, const string &localip) {
    fatalif(state_ != State::Invalid && state_ != State::Closed && state_ != State::Failed, "current state is bad state to connect. state: %d", state_);
    destHost_ = host;
    destPort_ = port;
    isClient_ = true;
    connectTimeout_ = timeout;
    connectedTime_ = util::timeMilli();
    localIp_ = localip;
    Ip4Addr addr(host, port);
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    fatalif(fd < 0, "socket failed %d %s", errno, strerror(errno));
    int r = 0;
    if (localip.size()) {
        Ip4Addr addr(localip, 0);
        r = ::bind(fd, (struct sockaddr *) &addr.getAddr(), sizeof(struct sockaddr));
        error("bind to %s failed error %d %s", addr.toString().c_str(), errno, strerror(errno));
    }
    if (r == 0) {
        r = ::connect(fd, (sockaddr *) &addr.getAddr(), sizeof(sockaddr_in));
        if (r != 0 && errno != EINPROGRESS) {
            error("connect to %s error %d %s", addr.toString().c_str(), errno, strerror(errno));
        }
    }

    sockaddr_in local;
    socklen_t alen = sizeof(local);
    if (r == 0) {
        r = getsockname(fd, (sockaddr *) &local, &alen);
        if (r < 0) {
            error("getsockname failed %d %s", errno, strerror(errno));
        }
    }
    state_ = State::Handshaking;
    attach(loop, fd, Ip4Addr(local), addr);
    if (timeout) {
        TcpConnPtr con = shared_from_this();
        timeoutId_ = loop->runAfter(timeout, [con] {
            if (con->getState() == Handshaking) {
                con->channel_->close();
            }
        });
    }
}

void TcpConn::close() {
    if (channel_) {
        TcpConnPtr con = shared_from_this();
        getLoop()->safeCall([con] {
            if (con->channel_)
                con->channel_->close();
        });
    }
}

void TcpConn::cleanup(const TcpConnPtr &con) {
    if (readcb_ && input_.size()) {
        readcb_(con);
    }
    if (state_ == State::Handshaking) {
        state_ = State::Failed;
    } else {
        state_ = State::Closed;
    }
    trace("tcp closing %s - %s fd %d %d", local_.toString().c_str(), peer_.toString().c_str(), channel_ ? channel_->fd() : -1, errno);
    getLoop()->cancel(timeoutId_);
    if (statecb_) {
        statecb_(con);
    }
    if (reconnectInterval_ >= 0 && !getLoop()->exited()) {  // reconnect
        reconnect();
        return;
    }
    for (auto &idle : idleIds_) {
        titanUnregisterIdle(getLoop(), idle);
    }
    // channel may have hold TcpConnPtr, set channel_ to NULL before delete
    readcb_ = writablecb_ = statecb_ = nullptr;
    Channel *ch = channel_;
    channel_ = NULL;
    delete ch;
}

void TcpConn::handleRead(const TcpConnPtr &con) {
    if (state_ == State::Handshaking && handleHandshake(con)) {
        return;
    }
    while (state_ == State::Connected) {
        input_.makeRoom();
        int rd = 0;
        if (channel_->fd() >= 0) {
            rd = readImp(channel_->fd(), input_.end(), input_.space());
            trace("channel %lld fd %d readed %d bytes", (long long) channel_->id(), channel_->fd(), rd);
        }
        if (rd == -1 && errno == EINTR) {
            continue;
        } else if (rd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            for (auto &idle : idleIds_) {
                titanUpdateIdle(getLoop(), idle);
            }
            if (readcb_ && input_.size()) {
                readcb_(con);
            }
            break;
        } else if (channel_->fd() == -1 || rd == 0 || rd == -1) { // titan只有一种关闭连接的方法: 被动关闭. 即对方先关闭连接, 本地read返回0
            cleanup(con);
            break;
        } else {  // rd > 0
            input_.addSize(rd);
        }
    }
}

int TcpConn::handleHandshake(const TcpConnPtr &con) {
    fatalif(state_ != Handshaking, "handleHandshaking called when state_=%d", state_);
    struct pollfd pfd;
    pfd.fd = channel_->fd();
    pfd.events = POLLOUT | POLLERR;
    int r = poll(&pfd, 1, 0);
    if (r == 1 && pfd.revents == POLLOUT) {
        channel_->enableReadWrite(true, false);
        state_ = State::Connected;
        if (state_ == State::Connected) {
            connectedTime_ = util::timeMilli();
            trace("tcp connected %s - %s fd %d", local_.toString().c_str(), peer_.toString().c_str(), channel_->fd());
            if (statecb_) {
                statecb_(con);
            }
        }
    } else {
        trace("poll fd %d return %d revents %d", channel_->fd(), r, pfd.revents);
        cleanup(con);
        return -1;
    }
    return 0;
}

void TcpConn::handleWrite(const TcpConnPtr &con) {
    if (state_ == State::Handshaking) {
        handleHandshake(con);
    } else if (state_ == State::Connected) {
        ssize_t sended = isend(output_.begin(), output_.size());
        output_.consume(sended);
        if (output_.empty() && writablecb_) { // WirteCompleteCallback
            writablecb_(con);
        }
        if (output_.empty() && channel_->writeEnabled()) {  // writablecb_ may write something
            channel_->enableWrite(false); // 一旦发送完毕数据, 立刻停止writable事件, 避免busy loop
        }
    } else {
        error("handle write unexpected");
    }
}

ssize_t TcpConn::isend(const char *buf, size_t len) {
    size_t sended = 0;
    while (sended < len) {
        ssize_t wd = writeImp(channel_->fd(), buf + sended, len - sended);
        trace("channel %lld fd %d write %ld bytes", (long long) channel_->id(), channel_->fd(), wd);
        if (wd > 0) {
            sended += wd;
            continue;
        } else if (wd == -1 && errno == EINTR) {
            continue;
        } else if (wd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!channel_->writeEnabled()) {
                channel_->enableWrite(true); // 开始关注poller的可写事件
            }
            break;
        } else {
            error("write error: channel %lld fd %d wd %ld %d %s", (long long) channel_->id(), channel_->fd(), wd, errno, strerror(errno));
            break;
        }
    }
    return sended;
}

void TcpConn::send(Buffer &buf) {
    if (channel_) {
        if (channel_->writeEnabled()) {  // just full
            output_.absorb(buf);
        }
        if (buf.size()) {
            ssize_t sended = isend(buf.begin(), buf.size());
            buf.consume(sended);
        }
        if (buf.size()) {
            output_.absorb(buf);
            if (!channel_->writeEnabled()) {
                channel_->enableWrite(true);
            }
        }
    } else {
        warn("connection %s - %s closed, but still writing %lu bytes", local_.toString().c_str(), peer_.toString().c_str(), buf.size());
    }
}

void TcpConn::send(const char *buf, size_t len) {
    if (channel_) {
        if (output_.empty()) {
            ssize_t sended = isend(buf, len);
            buf += sended;
            len -= sended;
        }
        if (len) {
            output_.append(buf, len);
        }
    } else {
        warn("connection %s - %s closed, but still writing %lu bytes", local_.toString().c_str(), peer_.toString().c_str(), len);
    }
}

void TcpConn::setMsgCallback(CodecBase *codec, const MsgCallback &cb) {
    assert(!readcb_);
    codec_.reset(codec);
    setReadCallback([cb](const TcpConnPtr &con) {
        int r = 1;
        while (r) {
            Slice msg;
            r = con->codec_->tryDecode(con->getInput(), msg);
            if (r < 0) {
                con->channel_->close();
                break;
            } else if (r > 0) {
                trace("a msg decoded. origin len %d msg len %ld", r, msg.size());
                cb(con, msg);
                con->getInput().consume(r);
            }
        }
    });
}

void TcpConn::sendMsg(Slice msg) {
    codec_->encode(msg, getOutput());
    sendOutput();
}

TcpServer::TcpServer(EventLoopBases *bases) 
        : loop_(bases->allocEventLoop()), bases_(bases), listen_channel_(NULL), createcb_([] { return TcpConnPtr(new TcpConn); }) {}

int TcpServer::bind(const std::string &host, unsigned short port, bool reusePort) {
    addr_ = Ip4Addr(host, port);
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int r = net::setReuseAddr(fd); // net::setReuseAddr(fd, true)
    fatalif(r, "set socket reuse option failed");
    r = net::setReusePort(fd, reusePort);
    fatalif(r, "set socket reuse port option failed");
    r = ::bind(fd, (struct sockaddr *) &addr_.getAddr(), sizeof(struct sockaddr));
    if (r) {
        close(fd);
        error("bind to %s failed %d %s", addr_.toString().c_str(), errno, strerror(errno));
        return errno;
    }
    r = listen(fd, 20);
    fatalif(r, "listen failed %d %s", errno, strerror(errno));
    info("fd %d listening at %s", fd, addr_.toString().c_str());
    listen_channel_ = new Channel(loop_, fd, kReadEvent);
    listen_channel_->setReadCallback([this] { handleAccept(); });
    return 0;
}

TcpServerPtr TcpServer::startServer(EventLoopBases *bases, const std::string &host, unsigned short port, bool reusePort) {
    TcpServerPtr p(new TcpServer(bases));
    int r = p->bind(host, port, reusePort);
    if (r) {
        error("bind to %s:%d failed %d %s", host.c_str(), port, errno, strerror(errno));
    }
    return r == 0 ? p : NULL;
}

void TcpServer::handleAccept() {
    struct sockaddr_in raddr;
    socklen_t rsz = sizeof(raddr);
    int lfd = listen_channel_->fd();
    int cfd;
    while (lfd >= 0 && (cfd = accept(lfd, (struct sockaddr *) &raddr, &rsz)) >= 0) { // accept策略: 读一个, 读N个, 读完
        sockaddr_in local, peer;
        socklen_t alen = sizeof(peer);
        int r = getpeername(cfd, (sockaddr *) &peer, &alen);
        if (r < 0) {
            error("get peer name failed %d %s", errno, strerror(errno));
            continue;
        }
        r = getsockname(cfd, (sockaddr *) &local, &alen);
        if (r < 0) {
            error("getsockname failed %d %s", errno, strerror(errno));
            continue;
        }
        r = util::addFdFlag(cfd, FD_CLOEXEC);
        fatalif(r, "addFdFlag FD_CLOEXEC failed");
        /* 为cfd连接分配的EventLoop有2种策略: 
            a). 程序只用了一个EventLoop, 在这一个线程的一个EventLoop上同时处理accept新连接和在已有的连接上read/write
            b). EventLoopThreadPool. 一个主IO线程的EventLoop用来处理新连接, 并且为每个新连接分配一个独占的EventLoop, 
             并在一个新的线程上运行这个EventLoop 
        */
        EventLoop *newLoop = bases_->allocEventLoop(); 
        if (newLoop == loop_) { 
            addNewConn(newLoop, cfd, local, peer);
        } else {
            newLoop->safeCall(std::bind(&TcpServer::addNewConn, this, newLoop, cfd, local, peer)); // 在新连接自己的EventLoop上执行addcon任务
        }
    }
    if (lfd >= 0 && errno != EAGAIN && errno != EINTR) {
        warn("accept return %d  %d %s", cfd, errno, strerror(errno));
    }
}

void TcpServer::addNewConn(EventLoop *newLoop, int fd, sockaddr_in local, sockaddr_in peer) {
    TcpConnPtr con = createcb_();
    con->attach(newLoop, fd, local, peer);
    if (statecb_) {
        con->setStateCallback(statecb_);
    }
    if (readcb_) {
        con->setReadCallback(readcb_);
    }
    if (msgcb_) {
        con->setMsgCallback(codec_->clone(), msgcb_);
    }
};

}  // namespace titan
