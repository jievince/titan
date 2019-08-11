#pragma once
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <map>

namespace titan {

struct noncopyable {
   protected:
    noncopyable() = default;
    virtual ~noncopyable() = default;

    noncopyable(const noncopyable &) = delete;
    noncopyable &operator=(const noncopyable &) = delete;
};

struct util {
    static std::string format(const char *fmt, ...);
    static int64_t timeMicro();
    static int64_t timeMilli() { return timeMicro() / 1000; }
    static int64_t steadyMicro();
    static int64_t steadyMilli() { return steadyMicro() / 1000; }
    static std::string readableTime(time_t t);
    static int64_t atoi(const char *b, const char *e) { return strtol(b, (char **) &e, 10); }
    static int64_t atoi2(const char *b, const char *e) {
        char **ne = (char **) &e;
        int64_t v = strtol(b, ne, 10);
        return ne == (char **) &e ? v : -1;
    }
    static int64_t atoi(const char *b) { return atoi(b, b + strlen(b)); }
    static int addFdFlag(int fd, int flag);
};

struct ExitCaller : private noncopyable {
    ~ExitCaller() { functor_(); }
    ExitCaller(std::function<void()> &&functor) : functor_(std::move(functor)) {}

   private:
    std::function<void()> functor_;
};

struct Signal {
    static void signal(int sig, const std::function<void()> &handler);

    static std::map<int, std::function<void()>> handlers;
    static void signal_handler(int sig) {
        handlers[sig]();
    }
};

}  // namespace titan
