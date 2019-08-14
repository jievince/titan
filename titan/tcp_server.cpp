#include "tcp_server.h"
#include "tcp_conn.h"
#include <fcntl.h>
#include <poll.h>
#include "logging.h"
#include "poller.h"
#include "channel.h"

namespace titan {

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

void TcpServer::setTcpConnMsgCallback(CodecBase *codec, const MsgCallback &cb) {
    assert(!readcb_);
    codec_.reset(codec);
    setTcpConnReadCallback([cb](const TcpConnPtr &con) {
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

void TcpServer::handleAccept() {
    struct sockaddr_in raddr;
    socklen_t rsz = sizeof(raddr);
    int lfd = listen_channel_->fd();
    int cfd;
    // non-block accept returns cfd > 0 + poll() returns cfd is writable: connection is establised
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
};

} // namespace titan