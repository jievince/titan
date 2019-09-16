#pragma once
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <map>
#include <netinet/in.h>
#include <algorithm>
#include "port_posix.h"
#include "slice.h"

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

struct net {
    template <class T>
    static T hton(T v) {
        return port::htobe(v);
    }
    template <class T>
    static T ntoh(T v) {
        return port::htobe(v);
    }
    static int setNonBlock(int fd, bool value = true);
    static int setReuseAddr(int fd, bool value = true);
    static int setReusePort(int fd, bool value = true);
    static int setNoDelay(int fd, bool value = true);
};

struct Ip4Addr {
    Ip4Addr(const std::string &host, unsigned short port);
    Ip4Addr(unsigned short port = 0) : Ip4Addr("", port) {}
    Ip4Addr(const struct sockaddr_in &addr) : addr_(addr){};
    std::string toString() const;
    std::string ip() const; // 点分十进制ip
    unsigned short port() const;
    unsigned int ipInt() const;
    // if you pass a hostname to constructor, then use this to check error
    bool isIpValid() const;
    struct sockaddr_in &getAddr() {
        return addr_;
    }
    static std::string hostToIp(const std::string &host) {
        Ip4Addr addr(host, 0);
        return addr.ip();
    }

   private:
    struct sockaddr_in addr_;
};

struct Signal {
    static void signal(int sig, const std::function<void()> &handler);

    static std::map<int, std::function<void()>> handlers;
    static void signal_handler(int sig) {
        handlers[sig]();
    }
};

struct ExitCaller : private noncopyable {
    ~ExitCaller() { functor_(); }
    ExitCaller(std::function<void()> &&functor) : functor_(std::move(functor)) {}

   private:
    std::function<void()> functor_;
};

}  // namespace titan
