#pragma once
#include <algorithm>
#include "slice.h"

namespace titan {

struct Buffer {
    Buffer() : buf_(NULL), b_(0), e_(0), cap_(0), exp_(512) {}
    ~Buffer() { delete[] buf_; }
    void clear() {
        delete[] buf_;
        buf_ = NULL;
        cap_ = 0;
        b_ = e_ = 0;
    }
    size_t size() const { return e_ - b_; }
    bool empty() const { return e_ == b_; }
    char *data() const { return buf_ + b_; }
    char *begin() const { return buf_ + b_; }
    char *end() const { return buf_ + e_; }
    char *makeRoom(size_t len);
    void makeRoom() {
        if (space() < exp_)
            expand(0);
    }
    size_t space() const { return cap_ - e_; }
    void addSize(size_t len) { e_ += len; }
    Buffer &append(const char *p, size_t len) {
        char *dst = makeRoom(len);
        addSize(len);
        memcpy(dst, p, len);
        return *this;
    }
    Buffer &append(Slice slice) { return append(slice.data(), slice.size()); }
    Buffer &append(const char *p) { return append(p, strlen(p)); }
    template <class T>
    Buffer &appendValue(const T &v) {
        append((const char *) &v, sizeof v);
        return *this;
    }
    Buffer &consume(size_t len) {
        b_ += len;
        if (empty())
            clear();
        return *this;
    }
    Buffer &absorb(Buffer &buf);
    void setSuggestSize(size_t sz) { exp_ = sz; }
    Buffer(const Buffer &b) { copyFrom(b); }
    Buffer &operator=(const Buffer &b) {
        if (this != &b) {
            delete[] buf_;
            buf_ = NULL;
            copyFrom(b);
        }
        return *this;
    }
    operator Slice() { return Slice(data(), size()); }

   private:
    char *buf_;
    size_t b_, e_, cap_, exp_;
    void moveHead() {
        std::copy(begin(), end(), buf_);
        e_ -= b_;
        b_ = 0;
    }
    void expand(size_t len);
    void copyFrom(const Buffer &b);
};

inline char *Buffer::makeRoom(size_t len) {
    if (e_ + len <= cap_) {
    } else if (size() + len < cap_ / 2) {
        moveHead();
    } else {
        expand(len);
    }
    return end();
}

inline void Buffer::expand(size_t len) {
    size_t ncap = std::max(exp_, std::max(2 * cap_, size() + len));
    char *p = new char[ncap];
    std::copy(begin(), end(), p);
    e_ -= b_;
    b_ = 0;
    delete[] buf_;
    buf_ = p;
    cap_ = ncap;
}

inline void Buffer::copyFrom(const Buffer &b) {
    memcpy(this, &b, sizeof b);
    if (b.buf_) {
        buf_ = new char[cap_];
        memcpy(data(), b.begin(), b.size());
    }
}

inline Buffer &Buffer::absorb(Buffer &buf) {
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
