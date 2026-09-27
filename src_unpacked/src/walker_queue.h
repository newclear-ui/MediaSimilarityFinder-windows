#pragma once
// Node D3-Minimal: bounded walker queue.
//
// The scan's directory walker (fast producer) and analysis consumer
// previously shared an UNBOUNDED queue: large trees could grow it without
// limit. This class adds capacity + cancel-aware backpressure and nothing
// else: no worker pools, no scheduling policy, no result routing, no new
// execution queues (those remain explicitly out of D3-Minimal scope).
//
// Pause is deliberately NOT a queue concept: the scanner stalls upstream
// (scanner.cpp sleeps without holding locks) before onFile is ever called,
// so a paused producer never reaches push(). A producer blocked here while
// pause engages is woken by consumer progress after resume; nobody sleeps
// while holding the mutex, so no deadlock is possible either way.
//
// Cancel/shutdown wake waiters. Waits re-check on a 100 ms bound, so a
// missed notification can delay but never hang.
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include "database.h" // FileState
namespace msf {
class WalkerQueue {
public:
  // Safety bound, NOT a tuned optimum (see D3-Minimal history). Must be >= 1.
  static constexpr std::size_t kDefaultCapacity = 4096;
  explicit WalkerQueue(std::size_t capacity = kDefaultCapacity)
      : capacity_(capacity < 1 ? 1 : capacity) {}
  WalkerQueue(const WalkerQueue&) = delete;
  WalkerQueue& operator=(const WalkerQueue&) = delete;
  enum class PushResult { Pushed, Cancelled, Shutdown };
  // Blocks while full. Returns false-equivalents WITHOUT taking the item
  // on cancel/shutdown (a dropped walked file was never analyzed, so the
  // next scan simply sees it as new — same as an unwalked file on cancel).
  // `waited` reports whether this call blocked at least once.
  PushResult push(FileState item, const std::atomic_bool* cancel, bool* waited = nullptr);
  bool tryPop(FileState& out);
  // Consumer-side wait: true when data is available. Walk-done/cancel
  // stay engine-side (see waitForData callers); shutdown wakes unconditionally.
  bool waitForData(int timeoutMs);
  // Called once before the walker thread joins, on every exit path, so a
  // producer blocked at that moment always wakes. Idempotent.
  void shutdown();
  std::size_t size() const;
  bool empty() const;
  std::size_t capacity() const { return capacity_; }
  std::uint64_t blockedTicks() const;
private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<FileState> queue_;
  bool shutdown_ = false;
  std::uint64_t blockedTicks_ = 0;
};
}
