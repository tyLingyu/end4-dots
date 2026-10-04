#include "runtime/color.h"

#include <array>
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

} // namespace ii
