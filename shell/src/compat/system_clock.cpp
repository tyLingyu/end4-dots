#include "compat/system_clock.h"

#include "core/timer_manager.h"

#include <chrono>
#include <ctime>

namespace ii::qs {

  SystemClock::SystemClock() {
    enabled.changed().connectForever([this] { update(); });
    precision.changed().connectForever([this] { update(); });
    update();
  }

  SystemClock::~SystemClock() { TimerManager::instance().cancel(m_timerId); }

  // The local time of `ms` with the parts finer than the precision (and the milliseconds) zeroed.
  std::int64_t SystemClock::truncate(std::int64_t ms) const {
    const std::time_t secs = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    localtime_r(&secs, &tm);
    if (precision.peek() < Minutes) {
      tm.tm_min = 0;
    }
    if (precision.peek() < Seconds) {
      tm.tm_sec = 0;
    }
    tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm)) * 1000;
  }

  void SystemClock::update() {
    if (enabled.peek()) {
      setTime(0);
      schedule(0);
    } else {
      TimerManager::instance().cancel(m_timerId);
      m_timerId = 0;
    }
  }

  void SystemClock::onTimeout() {
    setTime(m_targetMs);
    schedule(m_targetMs);
  }

  void SystemClock::setTime(std::int64_t targetMs) {
    const std::int64_t now = DateTime::now().msecsSinceEpoch;
    const std::int64_t offset = targetMs - now;
    const std::int64_t current = truncate(offset > -500 && offset < 500 ? targetMs : now);
    const std::time_t secs = static_cast<std::time_t>(current / 1000);
    std::tm tm{};
    localtime_r(&secs, &tm);
    date.writeDirect(DateTime{current});
    hours.writeDirect(tm.tm_hour);
    minutes.writeDirect(tm.tm_min);
    seconds.writeDirect(tm.tm_sec);
  }

  void SystemClock::schedule(std::int64_t targetMs) {
    const std::int64_t now = DateTime::now().msecsSinceEpoch;
    const std::int64_t offset = targetMs - now;
    std::int64_t next = truncate(offset > 0 && offset < 500 ? targetMs : now);  // timer skew
    next += precision.peek() >= Seconds ? 1000 : precision.peek() >= Minutes ? 60'000 : 3'600'000;
    m_targetMs = next;
    m_timerId = TimerManager::instance().start(m_timerId, std::chrono::milliseconds(next - now), [this] { onTimeout(); });
  }

} // namespace ii::qs
