//#include <titan/titan.h>
#include </home/jackw/Documents/lambda/titan/titan/titan.h>
using namespace titan;

int main(int argc, const char *argv[]) {
    EventLoop base; //事件分发器
    Signal::signal(SIGINT, [&] { base.exit(); }); //注册Ctrl+C的信号处理器--退出事件分发循环. 将exit_置为true
    TcpServerPtr svr = TcpServer::startServer(&base, "", 2099); //创建服务器并绑定端口
    exitif(svr == NULL, "start tcp server failed");
    svr->setConnReadCallback([](const TcpConnPtr &con) { con->send(con->getInput()); });
    base.loop(); // 进入事件分发循环
}