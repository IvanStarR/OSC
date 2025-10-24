#include "thread_pool.hpp"
ThreadPool::ThreadPool(unsigned n){
  if (n==0) n = std::thread::hardware_concurrency();
  for (unsigned i=0;i<n;i++){
    workers.emplace_back([this]{
      for(;;){
        std::function<void()> job;
        { std::unique_lock<std::mutex> lk(m);
          cv.wait(lk,[&]{ return stop || !q.empty(); });
          if (stop && q.empty()) return;
          job = std::move(q.front()); q.pop();
        }
        job();
      }
    });
  }
}
ThreadPool::~ThreadPool(){
  { std::lock_guard<std::mutex> lk(m); stop=true; }
  cv.notify_all();
  for(auto& t:workers) t.join();
}
void ThreadPool::submit(std::function<void()> fn){
  { std::lock_guard<std::mutex> lk(m); q.emplace(std::move(fn)); }
  cv.notify_one();
}
void ThreadPool::wait_empty(){
  for(;;){
    { std::lock_guard<std::mutex> lk(m); if (q.empty()) break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}