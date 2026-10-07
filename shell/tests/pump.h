#pragma once

// Runs what the main loop runs (deferred calls, timers, FdWatch fds) for tests that have no
// MainLoop: until `done()` holds or `timeoutMs` passes. Returns whether `done()` held.

#include "app/poll_source.h"
#include "core/deferred_call.h"
#include "core/timer_manager.h"
#include "runtime/fd_watch.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <poll.h>
#include <vector>

namespace ii_test {

  inline void drainDeferred() {
    for (int round = 0; round < 10; ++round) {
      auto pending = DeferredCall::takePending();
      if (pending.empty()) {
        return;
      }
      for (auto& fn : pending) {
        fn();
      }
    }
  }

  inline bool pumpUntil(const std::function<bool()>& done, int timeoutMs = 2000) {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    PollSource& source = ii::FdWatch::pollSource();
    while (true) {
      drainDeferred();
      TimerManager::instance().tick();
      drainDeferred();
      if (done()) {
        return true;
      }
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
      if (left <= 0) {
        return false;
      }
      int wait = static_cast<int>(std::min<long long>(left, 50));
      if (const int timer = TimerManager::instance().pollTimeoutMs(); timer >= 0) {
        wait = std::min(wait, timer);
      }
      std::vector<pollfd> fds;
      const std::size_t start = source.addPollFds(fds);
      if (::poll(fds.data(), fds.size(), wait) > 0) {
        source.dispatch(fds, start);
      }
    }
  }

  // Runs the loop for `ms` regardless (to let events that shouldn't come prove they don't).
  inline void pumpFor(int ms) { (void)pumpUntil([] { return false; }, ms); }

} // namespace ii_test
