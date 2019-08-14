#include <titan/titan.h>
using namespace titan;

int main(int argc, const char *argv[]) {
    EventLoop loop;
    Signal::signal(SIGINT, [&] { loop.exit(); });
    info("program begin");
    loop.runAfter(200, []() { info("a task in runAfter 200ms"); });
    loop.runAfter(100, []() { info("a task in runAfter 100ms interval 1000ms"); }, 1000);
    TimerId id = loop.runAt(time(NULL) * 1000 + 300, []() { info("a task in runAt now+300 interval 500ms"); }, 500);
    loop.runAfter(2000, [&]() {
        info("cancel task of interval 500ms");
        loop.cancel(id);
    });
    loop.runAfter(3000, [&]() { loop.exit(); });
    loop.loop();
}