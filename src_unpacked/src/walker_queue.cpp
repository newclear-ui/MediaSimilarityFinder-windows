#include "walker_queue.h"
#include <chrono>
namespace msf {
WalkerQueue::PushResult WalkerQueue::push(FileState item, const std::atomic_bool* cancel, bool* waited) {
  bool everWaited = false;
  std::unique_lock<std::mutex> g(mutex_);
  while (queue_.size() >= capacity_) {
    everWaited = true;
    ++blockedTicks_;
    if (shutdown_) { if (waited) *waited = everWaited; return PushResult::Shutdown; }
    if (cancel && cancel->load(std::memory_order_relaxed)) {
      if (waited) *waited = everWaited;
      return PushResult::Cancelled;
    }
    cv_.wait_for(g, std::chrono::milliseconds(100));
  }
  if (shutdown_) { if (waited) *waited = everWaited; return PushResult::Shutdown; }
  if (cancel && cancel->load(std::memory_order_relaxed)) {
    if (waited) *waited = everWaited;
    return PushResult::Cancelled;
  }
  queue_.push(std::move(item));
  if (waited) *waited = everWaited;
  g.unlock();
  cv_.notify_one(); // a consumer may wait for data
  return PushResult::Pushed;
}
bool WalkerQueue::tryPop(FileState& out) {
  {
    std::lock_guard<std::mutex> g(mutex_);
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop();
  }
  cv_.notify_one(); // a producer may wait for room (notify after unlock)
  return true;
}
bool WalkerQueue::waitForData(int timeoutMs) {
  std::unique_lock<std::mutex> g(mutex_);
  cv_.wait_for(g, std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs),
               [this] { return !queue_.empty() || shutdown_; });
  return !queue_.empty();
}
void WalkerQueue::shutdown() {
  {
    std::lock_guard<std::mutex> g(mutex_);
    shutdown_ = true;
  }
  cv_.notify_all();
}
std::size_t WalkerQueue::size() const {
  std::lock_guard<std::mutex> g(mutex_);
  return queue_.size();
}
bool WalkerQueue::empty() const {
  std::lock_guard<std::mutex> g(mutex_);
  return queue_.empty();
}
std::uint64_t WalkerQueue::blockedTicks() const {
  std::lock_guard<std::mutex> g(mutex_);
  return blockedTicks_;
}
}
