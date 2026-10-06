#pragma once

// QML's date type (a JS Date): a point in time with millisecond precision, shown in local time.

#include <compare>
#include <cstdint>

namespace ii {

  struct DateTime {
    std::int64_t msecsSinceEpoch = 0;

    [[nodiscard]] static DateTime now();

    friend auto operator<=>(const DateTime&, const DateTime&) = default;
  };

} // namespace ii
