#include "threads.h"
#include <assert.h>
#include <utility>
using namespace std;

namespace titan {

template class SafeQueue<Task>;

ThreadPool::ThreadPool(int threads, int maxWaiting, bool start) : tasks_(maxWaiting), threads_(threads) {
    if (start) {
        this->start();
    }
}

ThreadPool::~ThreadPool() {
    assert(tasks_.exited());
    if (tasks_.size()) {
        fprintf(stderr, "%lu tasks not processed when thread pool exited\n", tasks_.size());
    }
}

void ThreadPool::start() {
    for (auto &th : threads_) { // 初始化vector<thread> threads_
        thread t([this] {
            while (!tasks_.exited()) { // 每个线程的线程函数就是当任务队列还没有退出时, 按顺序从头到尾执行任务队列中国的任务
                Task task;
                if (tasks_.pop_wait(&task)) {
                    task();
                }
            }
        });
        th.swap(t);
    }
}

void ThreadPool::join() {
    for (auto &t : threads_) {
        t.join();
    }
}

bool ThreadPool::addTask(Task &&task) {
    return tasks_.push(std::move(task)); // std::move不会真正地移动对象, 真正的移动操作是在移动构造函数与和移动赋值运算符函数完成的. std::move只是将参数转换为右值引用. 右值引用用作表达式时会变为左值
}

}  // namespace titan
