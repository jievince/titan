#include "tcp_conn.h"
#include <fcntl.h>
#include <poll.h>
#include "logging.h"
#include "poller.h"
#include "channel.h"

using namespace std;
namespace titan {

//void titanUnregisterIdle(EventLoop *loop, const IdleId &idle);
//void titanUpdateIdle(EventLoop *loop, const IdleId &idle);

TcpConn::TcpConn()
    : loop_(NULL), channel_(NULL), state_(State::Invalid), isClient_(false), connectTimeout_(0), reconnectInterval_(-1), connectedTime_(util::timeMilli()) {}

TcpConn::~TcpConn() {
    trace("tcp destroyed %s - %s", local_.toString().c_str(), peer_.toString().c_str());
    delete channel_;
}

void TcpConn::addIdleCB(int idle, const TcpCallback &cb) {
    if (channel_) {
        idleIds_.push_back(getLoop()->registerIdle(idle, shared_from_this(), cb));
    }
}

void TcpConn::reconnect() {
    auto con = shared_from_this();
    getLoop()->reconnectConns_.insert(con);
    long long interval = reconnectInterval_ - (util::timeMilli() - connectedTime_);
    interval = interval > 0 ? interval : 0;
    info("reconnect interval: %d will reconnect after %lld ms", reconnectInterval_, interval);
    getLoop()->runAfter(interval, [this, con]() {
        getLoop()->reconnectConns_.erase(con);
        connect(getLoop(), destHost_, destPort_, connectTimeout_, localIp_);
    });
    delete channel_; // "肉体还在, 灵魂不在了"
    channel_ = NULL;
}

void TcpConn::attach(EventLoop *loop, int fd, Ip4Addr local, Ip4Addr peer) {
    fatalif((!isClient_ && state_ != State::Invalid) || (isClient_ && state_ != State::Handshaking),
            "you should use a new TcpConn to attach. state: %d", state_);
    loop_ = loop;
    state_ = State::Handshaking;
    local_ = local;
    peer_ = peer;
    delete channel_;
    channel_ = new Channel(loop, fd, kWriteEvent | kReadEvent); //
    trace("tcp constructed %s - %s fd %d", local_.toString().c_str(), peer_.toString().c_str(), fd);
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
    if (localip.size()) { // client bind ip地址
        Ip4Addr addr(localip, 0); // sin_port置0, 会自动分配未占用端口号
        r = ::bind(fd, (struct sockaddr *) &addr.getAddr(), sizeof(struct sockaddr));
        error("bind to %s failed error %d %s", addr.toString().c_str(), errno, strerror(errno)); // bug
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

void TcpConn::close() { // thread-safe
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
    trace("tcp closing %s - %s fd %d errno %d(%s)", local_.toString().c_str(), peer_.toString().c_str(), channel_ ? channel_->fd() : -1, errno, strerror(errno));
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
    if (state_ == State::Handshaking) {
         handleHandshake(con);
    } else if (state_ == State::Connected) {
        while (1) {
            input_.makeRoom();
            int rd = 0;
            if (channel_->fd() >= 0) {
                rd = readImp(channel_->fd(), input_.end(), input_.space());
                trace("channel %lld fd %d readed %d bytes", (long long) channel_->id(), channel_->fd(), rd);
            }
            if (rd > 0) {
                input_.addSize(rd);
                continue;
            } else if (rd == -1 && errno == EINTR) {
                continue;
            } else if (rd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                for (auto &idle : idleIds_) {
                    titanUpdateIdle(getLoop(), idle);
                }
                if (readcb_ && input_.size()) { // 读完数据后调用readcb_从input_ Buffer中解码出消息, 执行回调
                    readcb_(con);
                }
                break;
            } else if (channel_->fd() == -1 || rd == 0 || rd == -1) { // channel_->fd() == -1: Channel::close() => handleRead(); titan只有一种关闭连接的方法: 被动关闭. 即对方先关闭连接, 本地read返回0
                cleanup(con);
                break;
            } else {
                error("read error: channel %lld fd %d rd %ld %d %s", (long long) channel_->id(), channel_->fd(), rd, errno, strerror(errno));
                break;
            }
        }
    } else {
        error("handle read unexpected");
    }
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
            channel_->enableWrite(false); // 一旦发送完毕数据(output_ Buffer中的数据), 立刻停止writable事件, 避免busy loop
        }
    } else {
        error("handle write unexpected");
    }
}

int TcpConn::handleHandshake(const TcpConnPtr &con) {
    fatalif(state_ != Handshaking, "handleHandshaking called when state_=%d", state_);
    struct pollfd pfd;
    pfd.fd = channel_->fd();
    pfd.events = POLLOUT | POLLERR;
    int r = poll(&pfd, 1, 0); // use poll or select to check if the connection is successfully established
    if (r == 1 && pfd.revents == POLLOUT) { // socket is writable
        int err = 0;
        socklen_t len = sizeof(errno);
        r = getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &err, (socklen_t*)&len);
        if (r < 0) {
            error("getsockname failed %d %s", errno, strerror(errno));
            return -1;
        }
        if (err != 0) {
            trace("fd %d connect failed %d %s ", channel_->fd(), err, strerror(err));
            cleanup(con);
            return -1;
        }
        state_ = State::Connected;
        channel_->enableReadWrite(true, false); // this connection is connected successfully! No need to care for KWriteEvent.
        connectedTime_ = util::timeMilli();
        trace("tcp connected %s - %s fd %d", local_.toString().c_str(), peer_.toString().c_str(), channel_->fd());
        if (statecb_) {
            statecb_(con);
        }
    } else {
        trace("poll fd %d return %d revents %d", channel_->fd(), r, pfd.revents);
        cleanup(con);
        return -1;
    }
    return 0;
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
        // 为了保证数据的有效性, 如果output_ Buffer中仍有(上次的)数据未发送, 则不能使用isend直接发送数据, 而应append到output_中.
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

}  // namespace titan
