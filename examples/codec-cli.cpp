#include <titan/titan.h>

using namespace std;
using namespace titan;

int main(int argc, const char *argv[]) {
    setloglevel("TRACE");
    EventLoop base;
    Signal::signal(SIGINT, [&] { base.exit(); });
    TcpConnPtr con = TcpConn::createConnection(&base, "127.0.0.1", 2099, 3000);
    con->setReconnectInterval(3000);
    con->setMsgCallback(new LengthCodec, [](const TcpConnPtr &con, Slice msg) { info("recv msg: %.*s", (int) msg.size(), msg.data()); });
    con->setStateCallback([=](const TcpConnPtr &con) {
        info("setStateCallback called state: %d", con->getState());
        if (con->getState() == TcpConn::Connected) {
            con->sendMsg("hello");
        }
    });
    base.loop();
    info("program exited");
}