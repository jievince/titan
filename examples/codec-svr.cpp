#include <titan/titan.h>

using namespace std;
using namespace titan;

int main(int argc, const char *argv[]) {
    Logger::getLogger().setLogLevel(Logger::LTRACE);
    EventLoop base;
    Signal::signal(SIGINT, [&] { base.exit(); });

    TcpServerPtr echo = TcpServer::startServer(&base, "", 2099);
    exitif(echo == NULL, "start tcp server failed");
    echo->setConnCreateCallback([] {
        TcpConnPtr con(new TcpConn);
        con->setMsgCallback(new LengthCodec, [](const TcpConnPtr &con, Slice msg) {
            info("recv msg: %.*s", (int) msg.size(), msg.data());
            con->sendMsg(msg);
        });
        return con;
    });
    base.loop();
    info("program exited");
}