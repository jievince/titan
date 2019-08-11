#include <titan/titan.h>
#include <map>

using namespace std;
using namespace titan;

int main(int argc, const char *argv[]) {
    setloglevel("TRACE");
    map<intptr_t, TcpConnPtr> users;  // 生命周期比连接更长，必须放在Base前
    EventLoop base; // 事件派发器
    Signal::signal(SIGINT, [&] { base.exit(); }); // 注册信号处理器

    int userid = 1;
    TcpServerPtr chat = TcpServer::startServer(&base, "", 2099); // host == ""...
    exitif(chat == NULL, "start tcpserver failed");
    chat->setConnCreateCallback([&] { // function call
        TcpConnPtr con(new TcpConn);
        con->setStateCallback([&](const TcpConnPtr &con) {
            if (con->getState() == TcpConn::Connected) {
                con->context<int>() = userid;
                const char *welcome = "<id> <msg>: send msg to <id>\n<msg>: send msg to all\n\nhello %d";
                con->sendMsg(util::format(welcome, userid));
                users[userid] = con;
                userid++;
            } else if (con->getState() == TcpConn::Closed) {
                users.erase(con->context<int>());
            }
        });
        con->setMsgCallback(new LineCodec, [&](const TcpConnPtr &con, Slice msg) {
            if (msg.size() == 0) {  //忽略空消息
                return;
            }
            int cid = con->context<int>(); // cid是自己的id
            char *p = (char *) msg.data();
            intptr_t id = strtol(p, &p, 10);
            p += *p == ' ';  //忽略一个空格
            string resp = util::format("%ld# %.*s", cid, msg.end() - p, p);

            int sended = 0;
            if (id == 0) {  //发给其他所有用户
                for (auto &pc : users) {
                    if (pc.first != cid) {
                        sended++;
                        pc.second->sendMsg(resp);
                    }
                }
            } else {  //发给特定用户
                auto p1 = users.find(id);
                if (p1 != users.end()) {
                    sended++;
                    p1->second->sendMsg(resp);
                }
            }
            con->sendMsg(util::format("#sended to %d users", sended));
        });
        return con;
    });
    base.loop();
    info("program exited");
    return 0;
}
