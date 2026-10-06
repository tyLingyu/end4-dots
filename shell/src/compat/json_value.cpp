#include "compat/json_value.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <vector>

namespace ii::qs {

  namespace {

    // QByteArray::number(d, 'g', QLocale::FloatingPointShortest): shortest digits, exponent form
    // (two exponent digits at least) when the exponent is < -4 or >= the digit count.
    std::string qtNumber(double value) {
      if (value == 0.0) {
        return "0";
      }
      char buffer[64];
      const auto r = std::to_chars(buffer, buffer + sizeof(buffer), std::fabs(value), std::chars_format::scientific);
      const std::string_view text(buffer, r.ptr);
      const auto e = text.find('e');
      std::string digits;
      for (const char c : text.substr(0, e)) {
        if (c != '.') {
          digits += c;
        }
      }
      const int exponent = std::atoi(std::string(text.substr(e + 1)).c_str());
      const int k = static_cast<int>(digits.size());
      std::string out = value < 0 ? "-" : "";
      if (exponent < -4 || exponent >= k) {
        out += digits.substr(0, 1);
        if (k > 1) {
          out += "." + digits.substr(1);
        }
        char exp[8];
        std::snprintf(exp, sizeof(exp), "e%c%02d", exponent < 0 ? '-' : '+', std::abs(exponent));
        return out + exp;
      }
      if (exponent < 0) {
        return out + "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') + digits;
      }
      out += digits.substr(0, static_cast<std::size_t>(exponent + 1));
      if (k > exponent + 1) {
        out += "." + digits.substr(static_cast<std::size_t>(exponent + 1));
      }
      return out;
    }

    void quote(const std::string& text, std::string& out) {
      static constexpr char kHex[] = "0123456789abcdef";
      out += '"';
      for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
          if (c < 0x20) {
            out += "\\u00";
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
          } else {
            out += ch;
          }
        }
      }
      out += '"';
    }

    void write(const js::Json& value, int level, std::string& out) {
      const std::string indent(static_cast<std::size_t>(4 * level), ' ');
      const std::string inner(static_cast<std::size_t>(4 * (level + 1)), ' ');
      switch (value.type()) {
      case js::Json::value_t::object: {
        out += "{\n";
        std::vector<std::string> keys;
        for (const auto& [k, v] : value.items()) {
          keys.push_back(k);
        }
        std::ranges::sort(keys);  // QJsonObject keeps its keys sorted
        for (std::size_t i = 0; i < keys.size(); ++i) {
          out += inner;
          quote(keys[i], out);
          out += ": ";
          write(value.at(keys[i]), level + 1, out);
          out += i + 1 < keys.size() ? ",\n" : "\n";
        }
        out += indent + "}";
        break;
      }
      case js::Json::value_t::array: {
        out += "[\n";
        for (std::size_t i = 0; i < value.size(); ++i) {
          out += inner;
          write(value[i], level + 1, out);
          out += i + 1 < value.size() ? ",\n" : "\n";
        }
        out += indent + "]";
        break;
      }
      case js::Json::value_t::string: quote(value.get_ref<const std::string&>(), out); break;
      case js::Json::value_t::boolean: out += value.get<bool>() ? "true" : "false"; break;
      case js::Json::value_t::number_integer: out += std::to_string(value.get<std::int64_t>()); break;
      case js::Json::value_t::number_unsigned: out += std::to_string(value.get<std::uint64_t>()); break;
      case js::Json::value_t::number_float: {
        const double d = value.get<double>();
        if (!std::isfinite(d)) {
          out += "null";
        } else if (d == std::trunc(d) && std::fabs(d) <= 9007199254740992.0) {
          out += std::to_string(static_cast<std::int64_t>(d));  // QJsonValue stores it as an integer
        } else {
          out += qtNumber(d);
        }
        break;
      }
      default: out += "null"; break;
      }
    }

  } // namespace

  std::string qtJsonIndented(const js::Json& json) {
    std::string out;
    write(json, 0, out);
    return out + "\n";
  }

  js::Json toQtJson(const Color& value) {
    const auto channel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
    char buffer[16];
    const int a = channel(value.a);
    if (a == 255) {
      std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", channel(value.r), channel(value.g), channel(value.b));
    } else {
      std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", a, channel(value.r), channel(value.g), channel(value.b));
    }
    return buffer;
  }

  bool fromQtJson(const js::Json& json, bool& out) {
    // QVariant converts anything to bool (a string is true unless empty, "0" or "false").
    if (json.is_boolean()) {
      out = json.get<bool>();
    } else if (json.is_number()) {
      out = json.get<double>() != 0.0;
    } else if (json.is_string()) {
      const auto& s = json.get_ref<const std::string&>();
      out = !(s.empty() || s == "0" || s == "false");
    } else {
      return false;
    }
    return true;
  }

  bool fromQtJson(const js::Json& json, double& out) {
    if (json.is_number()) {
      out = json.get<double>();
    } else if (json.is_boolean()) {
      out = json.get<bool>() ? 1.0 : 0.0;
    } else if (json.is_string()) {
      const double d = js::toNumber(json.get<std::string>());
      if (std::isnan(d) || json.get_ref<const std::string&>().empty()) {
        return false;
      }
      out = d;
    } else {
      return false;
    }
    return true;
  }

  bool fromQtJson(const js::Json& json, int& out) {
    double d = 0.0;
    if (!fromQtJson(json, d)) {
      return false;
    }
    out = static_cast<int>(std::lround(d));  // QVariant rounds (qRound): 2.7 -> 3, -2.5 -> -3
    return true;
  }

  bool fromQtJson(const js::Json& json, std::string& out) {
    if (json.is_string()) {
      out = json.get<std::string>();
    } else if (json.is_boolean()) {
      out = json.get<bool>() ? "true" : "false";
    } else if (json.is_number()) {
      out = js::toString(json.get<double>());
    } else {
      return false;
    }
    return true;
  }

  bool fromQtJson(const js::Json& json, Color& out) {
    if (!json.is_string()) {
      return false;
    }
    const auto parsed = parseQmlColor(json.get<std::string>());
    if (!parsed) {
      return false;
    }
    out = *parsed;
    return true;
  }

} // namespace ii::qs
