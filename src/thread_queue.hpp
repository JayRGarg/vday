#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>

namespace vday {

template <typename T>
class ThreadSafeQueue {
 public:
  void Push(const T& value) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push_back(value);
    }
    cv_.notify_one();
  }

  bool TryPop(T& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return false;
    }
    out = queue_.front();
    queue_.pop_front();
    return true;
  }

  bool WaitPop(T& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] { return !queue_.empty(); });
    out = queue_.front();
    queue_.pop_front();
    return true;
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<T> queue_;
};

}  // namespace vday
