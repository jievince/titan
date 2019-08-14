#include <titan/titan.h>
using namespace titan;

int main(int argc, const char *argv[]) {
    setloglevel("TRACE");
    EventLoop loop;
    Signal::signal(SIGINT, [&] { loop.exit(); });
    TcpServerPtr svr = TcpServer::startServer(&loop, "", 2099);
    exitif(svr == NULL, "start tcp server failed");
    svr->setTcpConnStateCallback([](const TcpConnPtr &con) {
        if (con->getState() == TcpConn::Connected) {
            con->addIdleCB(2, [](const TcpConnPtr &con) {
                info("idle for 2 seconds, close connection");
                con->close();
            });
        }
    });
    auto con = TcpConn::createConnection(&loop, "localhost", 2099);
    loop.runAfter(3000, [&]() { loop.exit(); });
    loop.loop();
}