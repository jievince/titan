#include <titan/titan.h>

using namespace std;
using namespace titan;

char buf[20 * 1024 * 1024];

int main(int argc, const char *argv[]) {
    setloglevel("TRACE");
    int sended = 0, total = 1054768 * 100;
    memset(buf, 'a', sizeof buf);
    EventLoop loop1;
    Signal::signal(SIGINT, [&] { loop1.exit(); });
    TcpServerPtr svr = TcpServer::startServer(&loop1, "", 2099);
    exitif(svr == NULL, "bind failed %d(%s)", errno, strerror(errno));
    char *last_buf = &buf[0];
    auto sendcb = [&](const TcpConnPtr &con) {
        while (con->getOutput().empty() && sended < total) {
            con->send(buf, sizeof buf);
            sended += sizeof buf;
            info("%d bytes sended, output size: %lu", sended, con->getOutput().size());
        }
        if (sended >= total) {
            con->close();
            loop1.exit();
        }
    };
    svr->setTcpConnStateCallback([sendcb](const TcpConnPtr &con){
        if (con->getState() == TcpConn::Connected) {
            con->setWriteCallback(sendcb);
        }
        sendcb(con);
    });
    thread th([] {  //模拟了一个客户端，连接服务器后，接收服务器发送过来的数据
        EventLoop loop2;
        TcpConnPtr con = TcpConn::createConnection(&loop2, "127.0.0.1", 2099);
        con->setReadCallback([](const TcpConnPtr &con) {
            info("recv %lu bytes", con->getInput().size());
            con->getInput().clear();
            sleep(1);
        });
        con->setStateCallback([&](const TcpConnPtr &con) {
            if (con->getState() == TcpConn::Closed || con->getState() == TcpConn::Failed) {
                loop2.exit();
            }
        });
        loop2.loop();
    });
    loop1.loop();
    th.join();
    info("program exited");
}