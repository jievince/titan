#include <titan/titan.h>

using namespace std;
using namespace titan;

int main(int argc, const char *argv[]) {
    int threads = 1;
    if (argc > 1) {
        threads = atoi(argv[1]);
    }
    setloglevel("TRACE");
    EventLoopThreadPool loops(threads); // one event loop one thread
    HttpServer sample(&loops);
    int r = sample.bind("", 8081);
    exitif(r, "bind failed %d %s", errno, strerror(errno));
    sample.onGet("/hello", [](const HttpConnPtr &con) {
        string v = con.getRequest().version;
        HttpResponse resp;
        resp.body = Slice("hello world");
        con.sendResponse(resp);
        if (v == "HTTP/1.0") {
            con->close();
        }
    });
    Signal::signal(SIGINT, [&] { loops.exit(); });
    loops.loop();
    return 0;
}
