#include <titan/titan.h>

using namespace std;
using namespace titan;

int main(int argc, const char *argv[]) {
    Logger::getLogger().setLogLevel(Logger::LTRACE);
    EventLoop loop;
    Signal::signal(SIGINT, [&] { loop.exit(); });

    TcpServerPtr echo = TcpServer::startServer(&loop, "", 2099);
    exitif(echo == NULL, "start tcp server failed");
    echo->setConnCreateCallback([] {
        TcpConnPtr con(new TcpConn);
        con->setMsgCallback(new LengthCodec, [](const TcpConnPtr &con, Slice msg) {
            info("recv msg: %.*s", (int) msg.size(), msg.data());
            con->sendMsg(msg);
        });
        return con;
    });
    loop.loop();
    info("program exited");
}