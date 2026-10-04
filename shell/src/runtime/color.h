#pragma once

#include "render/core/color.h"

#include <optional>
#include <string_view>

namespace ii {

  using Color = ::Color;

  // Parses a color the way QML does: "#RGB", "#RRGGBB", "#AARRGGBB" (alpha first, unlike
  // Noctalia's hex() which reads "#RRGGBBAA"), "transparent", and the common SVG names.
  [[nodiscard]] std::optional<Color> parseQmlColor(std::string_view text);

  // parseQmlColor() for literals known to be valid; invalid text yields opaque black, as QML does.
  [[nodiscard]] Color qmlColor(std::string_view text);

} // namespace ii
