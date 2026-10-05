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

  // QML's `Qt.*` colour functions and QColor's HSV/HSL accessors. Hues are 0..1, or -1 for an
  // achromatic colour (QColor::hsvHueF()); passing -1 to hsva/hsla gives an achromatic colour.
  namespace qt {
    [[nodiscard]] Color rgba(double r, double g, double b, double a = 1.0);
    [[nodiscard]] Color hsva(double h, double s, double v, double a = 1.0);
    [[nodiscard]] Color hsla(double h, double s, double l, double a = 1.0);
    [[nodiscard]] double hsvHue(const Color& c);
    [[nodiscard]] double hsvSaturation(const Color& c);
    [[nodiscard]] double hsvValue(const Color& c);
    [[nodiscard]] double hslHue(const Color& c);
    [[nodiscard]] double hslSaturation(const Color& c);
    [[nodiscard]] double hslLightness(const Color& c);
  } // namespace qt

} // namespace ii
