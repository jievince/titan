#include <algorithm>
#include "buffer.h"

namespace titan {

char *Buffer::makeRoom(size_t len) {
    if (e_ + len <= cap_) {
    } else if (size() + len < cap_ / 2) {
        moveHead();
    } else {
        expand(len);
    }
    return end();
}

void Buffer::expand(size_t len) {
    size_t ncap = std::max(exp_, std::max(2 * cap_, size() + len));
    char *p = new char[ncap];
    std::copy(begin(), end(), p);
    e_ -= b_;
    b_ = 0;
    delete[] buf_;
    buf_ = p;
    cap_ = ncap;
}

void Buffer::copyFrom(const Buffer &b) {
    memcpy(this, &b, sizeof b);
    if (b.buf_) {
        buf_ = new char[cap_];
        memcpy(data(), b.begin(), b.size());
    }
}

Buffer &Buffer::absorb(Buffer &buf) {
    if (&buf != this) {
        if (size() == 0) {
            char tmp[sizeof buf];
            // 交换两个Buffer对象实例
            memcpy(tmp, this, sizeof tmp);
            memcpy(this, &buf, sizeof tmp);
            memcpy(&buf, tmp, sizeof tmp);
            std::swap(exp_, buf.exp_);  // keep the origin exp_
        } else {
            append(buf.begin(), buf.size());
            buf.clear();
        }
    }
    return *this;
}

}  // namespace titan