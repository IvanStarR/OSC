#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> q;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> stop {false};

   public:
    explicit ThreadPool(unsigned n);
    ~ThreadPool();
    void submit(std::function<void()> fn);
    void wait_empty();
};
