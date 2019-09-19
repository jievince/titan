#include <titan/titan.h>
#include <sys/wait.h>

using namespace std;
using namespace titan;

struct Report {
    long connected;
    long retry;
    long sended;
    long recved;
    Report() { memset(this, 0, sizeof(*this)); }
};

int main(int argc, const char *argv[]) {
    if (argc < 6) {
        printf("usage %s <host> <port> <conn count> <create seconds> <heartbeat interval>\n",
               argv[0]);
        return 1;
    }
    int c = 1;
    string host = argv[c++];
    unsigned short port = (unsigned short) atoi(argv[c++]);
    int conn_count = atoi(argv[c++]);
    float create_seconds = atof(argv[c++]);
    int heartbeat_interval = atoi(argv[c++]);
    

    Signal::signal(SIGPIPE, [] {});
    EventLoop loop;
    char *buf = new char[100];
    ExitCaller ec1([=] { delete[] buf; });
    Slice msg(buf, 100);
    int connected = 0;
    int retry = 0;
    int sended = 0;
    int recved = 0;

    vector<TcpConnPtr> allConns;
    info("creating %d connections", conn_count);
    for (int k = 0; k < create_seconds * 10; k++) {
        loop.runAfter(100 * k, [&] {
            int c = conn_count / create_seconds / 10;
            for (int i = 0; i < c; i++) {
                auto con = TcpConn::createConnection(&loop, host, port, 20 * 1000);
                allConns.push_back(con);
                con->setReconnectInterval(20 * 1000);
                con->setMsgCallback(new LengthCodec, [&](const TcpConnPtr &con, const Slice &msg) {
                    ++recved;
                    if (heartbeat_interval == 0) { 
                        con->sendMsg(msg);
                        ++sended;
                    }
                });
                con->setStateCallback([&, i](const TcpConnPtr &con) {
                    TcpConn::State st = con->getState();
                    if (st == TcpConn::Connected) {
                        ++connected;
                    } else if (st == TcpConn::Closed || st == TcpConn::Failed) {
                        if (st == TcpConn::Closed) {
                            --connected;
                        }
                        ++retry;
                    }
                });
            }
        });
    }
    if (heartbeat_interval) {
        loop.runAfter(heartbeat_interval * 1000,
                    [&] {
                        for (int i = 0; i < heartbeat_interval * 10; i++) {
                            loop.runAfter(i * 100, [&, i] {
                                size_t block = allConns.size() / heartbeat_interval / 10;
                                for (size_t j = i * block; j < (i + 1) * block && j < allConns.size(); j++) {
                                    if (allConns[j]->getState() == TcpConn::Connected) {
                                        allConns[j]->sendMsg(msg);
                                            ++sended;
                                    }
                                }
                            });
                        }
                    },
                    heartbeat_interval * 1000);
    }
    loop.runAfter(1000,
                    [&]() {
                    info("connected: %6ld, retry: %6ld, sended: %6ld, recved: %6ld\n", connected, retry, sended, recved);
                    },
                    1000);
    loop.loop();


    info("program exited");
}
