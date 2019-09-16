#pragma once
#include <unistd.h>
#include <memory>
#include <set>
#include <utility>
#include "codec.h"
#include "logging.h"
#include "threads.h"
#include "util.h"

namespace titan {
struct Channel;
struct TcpConn;
struct TcpServer;
struct IdleIdImp;
struct EventsImp;
struct EventLoop;
typedef std::unique_ptr<IdleIdImp> IdleId;
typedef std::pair<int64_t, int64_t> TimerId;

}  // namespace titan