#pragma once

// Quickshell SystemClock (src/core/clock.cpp at 7511545, ported): the local time truncated to
// the precision, updated on each boundary.

#include "runtime/datetime.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <cstdint>

namespace ii::qs {

  class SystemClock : public Object {
  public:
    enum Enum { Hours = 0, Minutes = 1, Seconds = 2 };

    SystemClock();
    ~SystemClock() override;

    Property<bool> enabled{true};
    Property<Enum> precision{Seconds};
    Property<DateTime> date;  // read-only
    Property<int> hours;      // read-only
    Property<int> minutes;    // read-only
    Property<int> seconds;    // read-only

  private:
    void update();
    void onTimeout();
    void setTime(std::int64_t targetMs);
    void schedule(std::int64_t targetMs);
    [[nodiscard]] std::int64_t truncate(std::int64_t ms) const;

    std::uint64_t m_timerId = 0;
    std::int64_t m_targetMs = 0;
  };

} // namespace ii::qs
