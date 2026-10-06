#include "runtime/timer.h"

#include "core/deferred_call.h"
#include "core/timer_manager.h"

#include <chrono>

namespace ii {

  Timer::Timer() {
    running.changed().connectForever([this] {
      m_firstTick = true;
      update();
    });
    interval.changed().connectForever([this] { update(); });
    repeat.changed().connectForever([this] { update(); });
  }

  Timer::~Timer() {
    ++*m_generation;
    TimerManager::instance().cancel(m_timerId);
  }

  void Timer::restart() {
    running.writeDirect(false);
    running.writeDirect(true);
  }

  void Timer::componentComplete() { update(); }

  void Timer::update() {
    if (!isCompleted()) {
      return;
    }
    ++*m_generation;
    TimerManager::instance().cancel(m_timerId);
    m_timerId = 0;
    if (!running.peek()) {
      return;
    }
    const bool repeating = repeat.peek();
    m_timerId = TimerManager::instance().start(
        0, std::chrono::milliseconds(std::max(0, interval.peek())), [this] { expired(); }, repeating);
    if (triggeredOnStart.peek() && m_firstTick) {
      std::weak_ptr<std::uint64_t> generation = m_generation;
      const std::uint64_t expected = *m_generation;
      DeferredCall::callLater([this, generation, expected] {
        const auto alive = generation.lock();
        if (!alive || *alive != expected || !running.peek()) {
          return;
        }
        m_firstTick = false;
        triggered.emit();
      });
    }
  }

  void Timer::expired() {
    if (repeat.peek()) {
      if (running.peek()) {
        m_firstTick = false;
        triggered.emit();
      }
      return;
    }
    m_timerId = 0;
    if (!running.peek()) {
      return;
    }
    // QQmlTimer::finished(): stop first, then trigger.
    running.writeDirect(false);
    m_firstTick = false;
    triggered.emit();
  }

} // namespace ii
