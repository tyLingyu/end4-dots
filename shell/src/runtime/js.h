#pragma once

// JavaScript semantics that generated code (tools/qml2cpp) relies on.

#include <charconv>
#include <cmath>
#include <limits>
#include <string>

// A JavaScript expression qml2cpp could not translate. The translation belongs in
// tools/qml2cpp/data/js/<qml file>.json under this key; regenerating then replaces the stub.
#define II_TODO_JS(key) static_assert(false, "untranslated JavaScript " key " (tools/qml2cpp/data/js)")

namespace ii {

  // Math.round: halves round towards +Infinity (Math.round(-2.5) == -2), unlike std::round.
  [[nodiscard]] inline double jsRound(double value) { return std::floor(value + 0.5); }

  // Number -> string as JavaScript formats it (shortest round-trip, no trailing ".0").
  [[nodiscard]] inline std::string jsString(double value) {
    if (std::isnan(value)) {
      return "NaN";
    }
    if (std::isinf(value)) {
      return value > 0 ? "Infinity" : "-Infinity";
    }
    if (value == 0.0) {
      return "0";
    }
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
  }

  [[nodiscard]] inline std::string jsString(int value) { return std::to_string(value); }

} // namespace ii
