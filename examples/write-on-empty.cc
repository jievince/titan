#include <titan/titan.h>

using namespace std;
using namespace titan;

char buf[20 * 1024 * 1024];

int main(int argc, const char *argv[]) {
    setloglevel("TRACE");
    int sended = 0, total = 1054768 * 100;
    memset(buf, 'a', sizeof buf);
    EventLoop bases;
    Signal::signal(SIGINT, [&] { bases.exit(); });
    TcpServer echo(&bases);
    int r = echo.bind("", 2099);
    exitif(r, "bind failed %d %s", errno, strerror(errno));
    auto sendcb = [&](const TcpConnPtr &con) {
        while (con->getOutput().size() == 0 && sended < total) {
            con->send(buf, sizeof buf);
            sended += sizeof buf;
            info("%d bytes sended output size: %lu", sended, con->getOutput().size());
        }
        if (sended >= total) {
            con->close();
            bases.exit();
        }
    };
    echo.setConnCreateCallback([sendcb]() {
        TcpConnPtr con(new TcpConn);
        con->setStateCallback([sendcb](const TcpConnPtr &con) {
            if (con->getState() == TcpConn::Connected) {
                con->setWriteCallback(sendcb);
            }
            sendcb(con);
        });
        return con;
    });
    thread th([] {  //模拟了一个客户端，连接服务器后，接收服务器发送过来的数据
        EventLoop base2;
        TcpConnPtr con = TcpConn::createConnection(&base2, "127.0.0.1", 2099);
        con->onRead([](const TcpConnPtr &con) {
            info("recv %lu bytes", con->getInput().size());
            con->getInput().clear();
            sleep(1);
        });
        con->setStateCallback([&](const TcpConnPtr &con) {
            if (con->getState() == TcpConn::Closed || con->getState() == TcpConn::Failed) {
                base2.exit();
            }
        });
        base2.loop();
    });
    bases.loop();
    th.join();
    info("program exited");
}