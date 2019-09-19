#include <titan/titan.h>

using namespace std;
using namespace titan;

int main(int argc, const char *argv[]) {
    if (argc < 2) {
        printf("usage: %s <listen port>\n", argv[0]);
        return 1;
    }

    unsigned short port = (unsigned short) atoi(argv[1]);
    int connected = 0;
    int closed = 0;
    int recved = 0;
    EventLoop loop;
    TcpServerPtr svr = TcpServer::startServer(&loop, "", port);
    svr->setTcpConnStateCallback([&](const TcpConnPtr &con) {
        auto st = con->getState();
        if (st == TcpConn::Connected) {
            ++connected;
        } else if (st == TcpConn::Closed || st == TcpConn::Failed) {
            if (st == TcpConn::Closed) {
                --connected;
            }
            ++closed;
        }
    });
    svr->setTcpConnMsgCallback(new LengthCodec, [&](const TcpConnPtr &con, Slice msg) {
        ++recved;
        con->sendMsg(msg);
    });

    loop.runAfter(1000,
                  [&]() {
                    info("connected: %6ld, closed: %6ld recved: %6ld\n", connected, closed, recved);
                  },
                  1000);
    loop.loop();
    info("program exited");
}
