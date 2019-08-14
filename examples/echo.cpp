#include <titan/titan.h>

#include <iostream>
using namespace titan;

int main(int argc, const char *argv[]) {
    EventLoop loop; //事件分发器
    Signal::signal(SIGINT, [&] { loop.exit(); }); //注册Ctrl+C的信号处理器--退出事件分发循环. 将exit_置为true
    TcpServerPtr svr = TcpServer::startServer(&loop, "", 2099); //创建服务器并绑定端口
    exitif(svr == NULL, "start tcp server failed");
    svr->setTcpConnReadCallback([](const TcpConnPtr &con) { auto it = con->getInput().begin(); for (; it != con->getInput().end(); ++it) std::cout << *it; con->send(con->getInput()); });
    loop.loop(); // 进入事件分发循环
}