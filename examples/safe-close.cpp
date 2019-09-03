#include <titan/titan.h>
using namespace titan;

int main(int argc, const char *argv[]) {
    setloglevel("TRACE");
    EventLoop loop;
    Signal::signal(SIGINT, [&] { loop.exit(); });
    TcpServerPtr svr = TcpServer::startServer(&loop, "", 2099);
    exitif(svr == NULL, "start tcp server failed");
    TcpConnPtr con = TcpConn::createConnection(&loop, "localhost", 2099);
    std::thread th([con, &loop]() {
        sleep(1);
        info("thread want to close an connection");
        con->close(); // TcpConnn::close是线程安全的, 可以跨线程调用的.
        //loop.safeCall([con]() { con->close(); });  //其他线程需要操作连接，应当通过safeCall把操作交给io线程来做
    });
    loop.runAfter(1500, [&loop]() { loop.exit(); });
    loop.loop();
    th.join();
}