#include "runtime/color.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>

namespace ii {

  namespace {
    std::optional<std::uint32_t> hexDigit(char c) {
      if (c >= '0' && c <= '9') {
        return static_cast<std::uint32_t>(c - '0');
      }
      const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (lower >= 'a' && lower <= 'f') {
        return static_cast<std::uint32_t>(lower - 'a' + 10);
      }
      return std::nullopt;
    }

    std::optional<std::uint32_t> hexValue(std::string_view digits) {
      std::uint32_t value = 0;
      for (char c : digits) {
        auto d = hexDigit(c);
        if (!d) {
          return std::nullopt;
        }
        value = (value << 4U) | *d;
      }
      return value;
    }

    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 18> kNamed{{
        {"black", 0x000000},  {"white", 0xffffff},   {"red", 0xff0000},     {"green", 0x008000},
        {"lime", 0x00ff00},   {"blue", 0x0000ff},    {"yellow", 0xffff00},  {"cyan", 0x00ffff},
        {"magenta", 0xff00ff}, {"gray", 0x808080},   {"grey", 0x808080},    {"darkgray", 0xa9a9a9},
        {"lightgray", 0xd3d3d3}, {"orange", 0xffa500}, {"purple", 0x800080}, {"pink", 0xffc0cb},
        {"brown", 0xa52a2a},  {"silver", 0xc0c0c0},
    }};
  } // namespace

  std::optional<Color> parseQmlColor(std::string_view text) {
    if (text.empty()) {
      return std::nullopt;
    }
    if (text.front() == '#') {
      const std::string_view digits = text.substr(1);
      auto value = hexValue(digits);
      if (!value) {
        return std::nullopt;
      }
      switch (digits.size()) {
      case 3: {
        const auto r = (*value >> 8U) & 0xFU;
        const auto g = (*value >> 4U) & 0xFU;
        const auto b = *value & 0xFU;
        return Color{colorByte(r * 17U), colorByte(g * 17U), colorByte(b * 17U), 1.0F};
      }
      case 6:
        return rgbHex(*value);
      case 8:
        return Color{
            colorByte((*value >> 16U) & 0xFFU), colorByte((*value >> 8U) & 0xFFU), colorByte(*value & 0xFFU),
            colorByte((*value >> 24U) & 0xFFU)
        };
      default:
        return std::nullopt;
      }
    }
    std::string lower(text);
    for (char& c : lower) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "transparent") {
      return Color{0.0F, 0.0F, 0.0F, 0.0F};
    }
    for (const auto& [name, rgb] : kNamed) {
      if (name == lower) {
        return rgbHex(rgb);
      }
    }
    return std::nullopt;
  }

  Color qmlColor(std::string_view text) { return parseQmlColor(text).value_or(Color{0.0F, 0.0F, 0.0F, 1.0F}); }

  namespace qt {

    namespace {
      struct Extremes {
        double max;
        double min;
      };

      Extremes extremes(const Color& c) {
        return {std::max({double(c.r), double(c.g), double(c.b)}), std::min({double(c.r), double(c.g), double(c.b)})};
      }

      // Hue in 0..1 from RGB, or -1 when achromatic.
      double hue(const Color& c) {
        const auto [max, min] = extremes(c);
        const double delta = max - min;
        if (delta <= 0.0) {
          return -1.0;
        }
        double h;
        if (max == c.r) {
          h = (c.g - c.b) / delta;
        } else if (max == c.g) {
          h = 2.0 + (c.b - c.r) / delta;
        } else {
          h = 4.0 + (c.r - c.g) / delta;
        }
        h /= 6.0;
        return h < 0.0 ? h + 1.0 : h;
      }

      Color fromHueChromaMatch(double h, double chroma, double match, double a) {
        if (h < 0.0) {
          return rgba(match, match, match, a);
        }
        const double sector = std::fmod(h, 1.0) * 6.0;
        const double x = chroma * (1.0 - std::fabs(std::fmod(sector, 2.0) - 1.0));
        double r = 0.0;
        double g = 0.0;
        double b = 0.0;
        switch (static_cast<int>(sector)) {
        case 0: r = chroma; g = x; break;
        case 1: r = x; g = chroma; break;
        case 2: g = chroma; b = x; break;
        case 3: g = x; b = chroma; break;
        case 4: r = x; b = chroma; break;
        default: r = chroma; b = x; break;
        }
        return rgba(r + match, g + match, b + match, a);
      }
    } // namespace

    Color rgba(double r, double g, double b, double a) {
      return Color{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), static_cast<float>(a)};
    }

    Color hsva(double h, double s, double v, double a) {
      const double chroma = v * s;
      return fromHueChromaMatch(s <= 0.0 ? -1.0 : h, chroma, v - chroma, a);
    }

    Color hsla(double h, double s, double l, double a) {
      const double chroma = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
      return fromHueChromaMatch(s <= 0.0 ? -1.0 : h, chroma, l - chroma / 2.0, a);
    }

    double hsvHue(const Color& c) { return hue(c); }

    double hsvSaturation(const Color& c) {
      const auto [max, min] = extremes(c);
      return max <= 0.0 ? 0.0 : (max - min) / max;
    }

    double hsvValue(const Color& c) { return extremes(c).max; }

    double hslHue(const Color& c) { return hue(c); }

    double hslSaturation(const Color& c) {
      const auto [max, min] = extremes(c);
      const double l = (max + min) / 2.0;
      const double delta = max - min;
      if (delta <= 0.0) {
        return 0.0;
      }
      return delta / (1.0 - std::fabs(2.0 * l - 1.0));
    }

    double hslLightness(const Color& c) {
      const auto [max, min] = extremes(c);
      return (max + min) / 2.0;
    }

  } // namespace qt

} // namespace ii
