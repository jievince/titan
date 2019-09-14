#include <titan/titan.h>
using namespace titan;

int main(int argc, const char *argv[]) {
    setloglevel("TRACE");
    EventLoop loop;
    Signal::signal(SIGINT, [&] { loop.exit(); });
    TcpServerPtr svr = TcpServer::startServer(&loop, "", 2099);
    exitif(svr == NULL, "start tcp server failed");
    svr->setTcpConnStateCallback([&](const TcpConnPtr &con) {  // 200ms后关闭连接
        if (con->getState() == TcpConn::Connected)
            loop.runAfter(200, [con]() {
                info("close con after 200ms");
                con->close();
            });
    });
    TcpConnPtr con1 = TcpConn::createConnection(&loop, "localhost", 2099);
    con1->setReconnectInterval(300);
       TcpConnPtr con2 = TcpConn::createConnection(&loop, "localhost", 1, 100);
       con2->setReconnectInterval(200);
    loop.runAfter(600, [&]() { loop.exit(); });
    loop.loop();
}