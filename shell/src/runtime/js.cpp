#include "runtime/js.h"

#include "core/log.h"

#include <glib.h>

#include <array>
#include <cstdlib>
#include <stdexcept>

namespace ii::js {

  namespace {

    // ── Numbers ──────────────────────────────────────────────────────────────

    // Shortest round-trip digits of a positive finite value and its decimal exponent n, such
    // that value = 0.d1d2...dk × 10^n (the k and n of Number::toString in ECMA-262).
    void shortestDigits(double value, std::string& digits, int& n) {
      char buffer[64];
      const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::scientific);
      const std::string_view text(buffer, result.ptr);
      const auto e = text.find('e');
      digits.clear();
      for (const char c : text.substr(0, e)) {
        if (c != '.') {
          digits += c;
        }
      }
      n = std::atoi(std::string(text.substr(e + 1)).c_str()) + 1;
    }

    bool isLineTerminator(char32_t c) { return c == 0x0A || c == 0x0D || c == 0x2028 || c == 0x2029; }

    // WhiteSpace and LineTerminator of ECMA-262 (Zs from Unicode).
    bool isJsSpace(char32_t c) {
      switch (c) {
      case 0x09: case 0x0B: case 0x0C: case 0x20: case 0xA0: case 0xFEFF: case 0x1680:
      case 0x202F: case 0x205F: case 0x3000:
        return true;
      default:
        return (c >= 0x2000 && c <= 0x200A) || isLineTerminator(c);
      }
    }

    // Decodes one UTF-8 sequence at text[i], advancing i; malformed bytes decode to U+FFFD.
    char32_t decode(std::string_view text, std::size_t& i) {
      const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(text[k]); };
      const unsigned char b0 = byte(i);
      int extra = 0;
      char32_t c = 0;
      if (b0 < 0x80) {
        ++i;
        return b0;
      }
      if ((b0 & 0xE0) == 0xC0) {
        extra = 1, c = b0 & 0x1F;
      } else if ((b0 & 0xF0) == 0xE0) {
        extra = 2, c = b0 & 0x0F;
      } else if ((b0 & 0xF8) == 0xF0) {
        extra = 3, c = b0 & 0x07;
      } else {
        ++i;
        return 0xFFFD;
      }
      for (int k = 1; k <= extra; ++k) {
        if (i + k >= text.size() || (byte(i + k) & 0xC0) != 0x80) {
          ++i;
          return 0xFFFD;
        }
        c = (c << 6) | (byte(i + k) & 0x3F);
      }
      i += extra + 1;
      return c;
    }

    void encode(char32_t c, std::string& out) {
      if (c < 0x80) {
        out += static_cast<char>(c);
      } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
      } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
      } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
      }
    }

    // Byte offset where leading whitespace ends.
    std::size_t leadingSpaceEnd(std::string_view text) {
      std::size_t i = 0;
      while (i < text.size()) {
        std::size_t next = i;
        if (!isJsSpace(decode(text, next))) {
          break;
        }
        i = next;
      }
      return i;
    }

    // Byte offset where trailing whitespace starts.
    std::size_t trailingSpaceStart(std::string_view text) {
      std::size_t i = 0;
      std::size_t end = 0;
      while (i < text.size()) {
        std::size_t next = i;
        if (!isJsSpace(decode(text, next))) {
          end = next;
        }
        i = next;
      }
      return end;
    }

    int clampIndex(int index, int size) { return std::clamp(index, 0, size); }

    int digitValue(char c) {
      if (c >= '0' && c <= '9') {
        return c - '0';
      }
      if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
      }
      if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
      }
      return 99;
    }

    // Length of the longest StrDecimalLiteral prefix (sign excluded): digits, fraction, exponent.
    std::size_t decimalPrefix(std::string_view text) {
      std::size_t i = 0;
      std::size_t intDigits = 0;
      std::size_t fracDigits = 0;
      while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        ++i, ++intDigits;
      }
      if (i < text.size() && text[i] == '.') {
        std::size_t j = i + 1;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
          ++j, ++fracDigits;
        }
        if (intDigits + fracDigits > 0) {
          i = j;
        }
      }
      if (intDigits + fracDigits == 0) {
        return 0;
      }
      if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        std::size_t j = i + 1;
        if (j < text.size() && (text[j] == '+' || text[j] == '-')) {
          ++j;
        }
        const std::size_t expStart = j;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
          ++j;
        }
        if (j > expStart) {
          i = j;
        }
      }
      return i;
    }

    double parseDecimal(std::string_view text) {
      const std::string copy(text);
      return std::strtod(copy.c_str(), nullptr);
    }

    std::string expandReplacement(std::string_view replacement, std::string_view matched, std::string_view before,
                                  std::string_view after) {
      std::string out;
      for (std::size_t i = 0; i < replacement.size(); ++i) {
        if (replacement[i] != '$' || i + 1 >= replacement.size()) {
          out += replacement[i];
          continue;
        }
        switch (replacement[i + 1]) {
        case '$': out += '$'; ++i; break;
        case '&': out += matched; ++i; break;
        case '`': out += before; ++i; break;
        case '\'': out += after; ++i; break;
        default: out += '$'; break;
        }
      }
      return out;
    }

    std::vector<std::string> groups(const std::cmatch& m) {
      std::vector<std::string> out;
      for (std::size_t i = 0; i < m.size(); ++i) {
        out.push_back(m[i].matched ? m[i].str() : std::string());
      }
      return out;
    }

    void quote(std::string_view text, std::string& out) {
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

    void stringifyTo(const Json& value, int indent, int depth, std::string& out) {
      const auto newline = [&](int level) {
        if (indent > 0) {
          out += '\n';
          out.append(static_cast<std::size_t>(indent * level), ' ');
        }
      };
      switch (value.type()) {
      case Json::value_t::null:
      case Json::value_t::discarded:
        out += "null";
        break;
      case Json::value_t::boolean:
        out += value.get<bool>() ? "true" : "false";
        break;
      case Json::value_t::number_integer:
        out += std::to_string(value.get<std::int64_t>());
        break;
      case Json::value_t::number_unsigned:
        out += std::to_string(value.get<std::uint64_t>());
        break;
      case Json::value_t::number_float: {
        const double d = value.get<double>();
        out += std::isfinite(d) ? toString(d) : "null";
        break;
      }
      case Json::value_t::string:
        quote(value.get_ref<const std::string&>(), out);
        break;
      case Json::value_t::array: {
        if (value.empty()) {
          out += "[]";
          break;
        }
        out += '[';
        bool first = true;
        for (const auto& element : value) {
          if (!first) {
            out += ',';
          }
          first = false;
          newline(depth + 1);
          stringifyTo(element, indent, depth + 1, out);
        }
        newline(depth);
        out += ']';
        break;
      }
      case Json::value_t::object: {
        if (value.empty()) {
          out += "{}";
          break;
        }
        out += '{';
        bool first = true;
        for (const auto& [key, element] : value.items()) {
          if (!first) {
            out += ',';
          }
          first = false;
          newline(depth + 1);
          quote(key, out);
          out += indent > 0 ? ": " : ":";
          stringifyTo(element, indent, depth + 1, out);
        }
        newline(depth);
        out += '}';
        break;
      }
      case Json::value_t::binary:
        out += "null";
        break;
      }
    }

  } // namespace

  // ── Numbers ────────────────────────────────────────────────────────────────

  std::string toString(double value) {
    if (std::isnan(value)) {
      return "NaN";
    }
    if (value == 0.0) {
      return "0";
    }
    if (value < 0) {
      return "-" + toString(-value);
    }
    if (std::isinf(value)) {
      return "Infinity";
    }
    std::string digits;
    int n = 0;
    shortestDigits(value, digits, n);
    const int k = static_cast<int>(digits.size());
    if (k <= n && n <= 21) {
      return digits + std::string(static_cast<std::size_t>(n - k), '0');
    }
    if (0 < n && n <= 21) {
      return digits.substr(0, static_cast<std::size_t>(n)) + "." + digits.substr(static_cast<std::size_t>(n));
    }
    if (-6 < n && n <= 0) {
      return "0." + std::string(static_cast<std::size_t>(-n), '0') + digits;
    }
    const int e = n - 1;
    std::string out = digits.substr(0, 1);
    if (k > 1) {
      out += "." + digits.substr(1);
    }
    return out + "e" + (e < 0 ? "-" : "+") + std::to_string(std::abs(e));
  }

  std::string toString(double value, int radix) {
    if (radix == 10 || !std::isfinite(value) || value != std::trunc(value) || std::fabs(value) > 9007199254740992.0) {
      return toString(value);
    }
    static constexpr char kDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    auto magnitude = static_cast<std::uint64_t>(std::fabs(value));
    std::string out;
    do {
      out += kDigits[magnitude % static_cast<std::uint64_t>(radix)];
      magnitude /= static_cast<std::uint64_t>(radix);
    } while (magnitude > 0);
    if (value < 0) {
      out += '-';
    }
    std::reverse(out.begin(), out.end());
    return out;
  }

  std::string toFixed(double value, int digits) {
    if (std::isnan(value)) {
      return "NaN";
    }
    if (std::fabs(value) >= 1e21) {
      return toString(value);
    }
    const bool negative = value < 0;
    // The exact decimal expansion (a double has at most 1074 fractional digits).
    std::string text(1200, '\0');
    const auto result = std::to_chars(text.data(), text.data() + text.size(), std::fabs(value), std::chars_format::fixed, 1100);
    text.resize(static_cast<std::size_t>(result.ptr - text.data()));
    const auto point = text.find('.');
    std::string integer = text.substr(0, point);
    std::string fraction = text.substr(point + 1);
    const bool roundUp = fraction[static_cast<std::size_t>(digits)] >= '5';
    std::string kept = integer + fraction.substr(0, static_cast<std::size_t>(digits));
    if (roundUp) {
      int i = static_cast<int>(kept.size()) - 1;
      while (i >= 0 && kept[static_cast<std::size_t>(i)] == '9') {
        kept[static_cast<std::size_t>(i--)] = '0';
      }
      if (i < 0) {
        kept.insert(kept.begin(), '1');
      } else {
        ++kept[static_cast<std::size_t>(i)];
      }
    }
    const std::size_t intLength = kept.size() - static_cast<std::size_t>(digits);
    std::string out = kept.substr(0, intLength);
    if (digits > 0) {
      out += "." + kept.substr(intLength);
    }
    // JS: "If x < 0, let s be "-"" — so -0.0001.toFixed(2) is "-0.00".
    return negative ? "-" + out : out;
  }

  double toNumber(std::string_view text) {
    text = std::string_view(text).substr(leadingSpaceEnd(text));
    text = text.substr(0, trailingSpaceStart(text));
    if (text.empty()) {
      return 0.0;
    }
    if (text.size() > 2 && text[0] == '0') {
      const char p = static_cast<char>(text[1] | 0x20);
      const int radix = p == 'x' ? 16 : p == 'o' ? 8 : p == 'b' ? 2 : 0;
      if (radix != 0) {
        double result = 0;
        for (const char c : text.substr(2)) {
          const int d = digitValue(c);
          if (d >= radix) {
            return NaN;
          }
          result = result * radix + d;
        }
        return result;
      }
    }
    double sign = 1.0;
    std::string_view body = text;
    if (body[0] == '+' || body[0] == '-') {
      sign = body[0] == '-' ? -1.0 : 1.0;
      body.remove_prefix(1);
    }
    if (body == "Infinity") {
      return sign * std::numeric_limits<double>::infinity();
    }
    if (body.empty() || decimalPrefix(body) != body.size()) {
      return NaN;
    }
    return sign * parseDecimal(body);
  }

  double parseInt(std::string_view text, int radix) {
    text = text.substr(leadingSpaceEnd(text));
    double sign = 1.0;
    if (!text.empty() && (text[0] == '+' || text[0] == '-')) {
      sign = text[0] == '-' ? -1.0 : 1.0;
      text.remove_prefix(1);
    }
    bool stripPrefix = true;
    if (radix != 0) {
      if (radix < 2 || radix > 36) {
        return NaN;
      }
      stripPrefix = radix == 16;
    } else {
      radix = 10;
    }
    if (stripPrefix && text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
      text.remove_prefix(2);
      radix = 16;
    }
    std::size_t end = 0;
    while (end < text.size() && digitValue(text[end]) < radix) {
      ++end;
    }
    if (end == 0) {
      return NaN;
    }
    if (radix == 10) {
      return sign * parseDecimal(text.substr(0, end));  // correctly rounded for long inputs
    }
    double result = 0;
    for (const char c : text.substr(0, end)) {
      result = result * radix + digitValue(c);
    }
    return sign * result;
  }

  double parseFloat(std::string_view text) {
    text = text.substr(leadingSpaceEnd(text));
    double sign = 1.0;
    std::string_view body = text;
    if (!body.empty() && (body[0] == '+' || body[0] == '-')) {
      sign = body[0] == '-' ? -1.0 : 1.0;
      body.remove_prefix(1);
    }
    if (body.starts_with("Infinity")) {
      return sign * std::numeric_limits<double>::infinity();
    }
    const std::size_t end = decimalPrefix(body);
    if (end == 0) {
      return NaN;
    }
    return sign * parseDecimal(body.substr(0, end));
  }

  // ── Strings ────────────────────────────────────────────────────────────────

  std::u16string toUtf16(std::string_view text) {
    std::u16string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
      const char32_t c = decode(text, i);
      if (c >= 0x10000) {
        out += static_cast<char16_t>(0xD800 + ((c - 0x10000) >> 10));
        out += static_cast<char16_t>(0xDC00 + ((c - 0x10000) & 0x3FF));
      } else {
        out += static_cast<char16_t>(c);
      }
    }
    return out;
  }

  std::string toUtf8(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
      char32_t c = text[i];
      if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
        c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00);
        ++i;
      } else if (c >= 0xD800 && c <= 0xDFFF) {
        c = 0xFFFD;  // a lone surrogate has no UTF-8 form
      }
      encode(c, out);
    }
    return out;
  }

  int length(std::string_view text) { return static_cast<int>(toUtf16(text).size()); }

  std::string slice(std::string_view text, int start, int end) {
    const std::u16string s = toUtf16(text);
    const auto from = detail::relative(start, s.size());
    const auto to = detail::relative(end, s.size());
    return from < to ? toUtf8(std::u16string_view(s).substr(from, to - from)) : std::string();
  }

  std::string substring(std::string_view text, int start, int end) {
    const std::u16string s = toUtf16(text);
    const int size = static_cast<int>(s.size());
    int from = clampIndex(start, size);
    int to = clampIndex(end, size);
    if (from > to) {
      std::swap(from, to);
    }
    return toUtf8(std::u16string_view(s).substr(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from)));
  }

  std::string substr(std::string_view text, int start, int count) {
    const std::u16string s = toUtf16(text);
    const auto from = detail::relative(start, s.size());
    const auto n = std::min<std::size_t>(static_cast<std::size_t>(std::max(count, 0)), s.size() - from);
    return toUtf8(std::u16string_view(s).substr(from, n));
  }

  std::string charAt(std::string_view text, int index) {
    const std::u16string s = toUtf16(text);
    if (index < 0 || static_cast<std::size_t>(index) >= s.size()) {
      return {};
    }
    return toUtf8(std::u16string_view(s).substr(static_cast<std::size_t>(index), 1));
  }

  double charCodeAt(std::string_view text, int index) {
    const std::u16string s = toUtf16(text);
    if (index < 0 || static_cast<std::size_t>(index) >= s.size()) {
      return NaN;
    }
    return s[static_cast<std::size_t>(index)];
  }

  int indexOf(std::string_view text, std::string_view search, int from) {
    if (text.empty()) {
      return -1;  // V4: even "".indexOf("") is -1
    }
    const std::u16string s = toUtf16(text);
    const std::u16string needle = toUtf16(search);
    const auto start = static_cast<std::size_t>(clampIndex(from, static_cast<int>(s.size())));
    const auto found = s.find(needle, start);
    return found == std::u16string::npos ? -1 : static_cast<int>(found);
  }

  int lastIndexOf(std::string_view text, std::string_view search, int from) {
    const std::u16string s = toUtf16(text);
    const std::u16string needle = toUtf16(search);
    const auto start = static_cast<std::size_t>(clampIndex(from, static_cast<int>(s.size())));
    const auto found = s.rfind(needle, start);
    return found == std::u16string::npos ? -1 : static_cast<int>(found);
  }

  bool includes(std::string_view text, std::string_view search, int from) {
    // Not indexOf() >= 0: V4's "".includes("") is true although "".indexOf("") is -1.
    const std::u16string s = toUtf16(text);
    const auto start = static_cast<std::size_t>(clampIndex(from, static_cast<int>(s.size())));
    return s.find(toUtf16(search), start) != std::u16string::npos;
  }

  bool startsWith(std::string_view text, std::string_view search, int position) {
    const std::u16string s = toUtf16(text);
    const std::u16string needle = toUtf16(search);
    const auto start = static_cast<std::size_t>(clampIndex(position, static_cast<int>(s.size())));
    return s.compare(start, needle.size(), needle) == 0 && start + needle.size() <= s.size();
  }

  bool endsWith(std::string_view text, std::string_view search, int endPosition) {
    const std::u16string s = toUtf16(text);
    const std::u16string needle = toUtf16(search);
    const auto end = static_cast<std::size_t>(clampIndex(endPosition, static_cast<int>(s.size())));
    return needle.size() <= end && s.compare(end - needle.size(), needle.size(), needle) == 0;
  }

  std::string trim(std::string_view text) {
    text = text.substr(leadingSpaceEnd(text));
    return std::string(text.substr(0, trailingSpaceStart(text)));
  }

  std::string toLowerCase(std::string_view text) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
      encode(static_cast<char32_t>(g_unichar_tolower(static_cast<gunichar>(decode(text, i)))), out);
    }
    return out;
  }

  std::string toUpperCase(std::string_view text) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
      encode(static_cast<char32_t>(g_unichar_toupper(static_cast<gunichar>(decode(text, i)))), out);
    }
    return out;
  }

  namespace {
    std::u16string filler(std::u16string_view fill, std::size_t count) {
      std::u16string out;
      while (out.size() < count && !fill.empty()) {
        out += fill;
      }
      out.resize(std::min(out.size(), count));
      return out;
    }
  } // namespace

  std::string padStart(std::string_view text, int targetLength, std::string_view fill) {
    const std::u16string s = toUtf16(text);
    if (targetLength <= static_cast<int>(s.size())) {
      return std::string(text);
    }
    return toUtf8(filler(toUtf16(fill), static_cast<std::size_t>(targetLength) - s.size()) + s);
  }

  std::string padEnd(std::string_view text, int targetLength, std::string_view fill) {
    const std::u16string s = toUtf16(text);
    if (targetLength <= static_cast<int>(s.size())) {
      return std::string(text);
    }
    return toUtf8(s + filler(toUtf16(fill), static_cast<std::size_t>(targetLength) - s.size()));
  }

  std::string repeat(std::string_view text, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) {
      out += text;
    }
    return out;
  }

  std::vector<std::string> split(std::string_view text, std::string_view separator, std::uint32_t limit) {
    std::vector<std::string> out;
    if (limit == 0) {
      return out;
    }
    if (separator.empty()) {
      const std::u16string s = toUtf16(text);
      for (std::size_t i = 0; i < s.size() && out.size() < limit; ++i) {
        out.push_back(toUtf8(std::u16string_view(s).substr(i, 1)));
      }
      return out;
    }
    std::size_t start = 0;
    for (auto found = text.find(separator); found != std::string_view::npos; found = text.find(separator, start)) {
      out.emplace_back(text.substr(start, found - start));
      if (out.size() == limit) {
        return out;
      }
      start = found + separator.size();
    }
    out.emplace_back(text.substr(start));
    return out;
  }

  std::string replace(std::string_view text, std::string_view pattern, std::string_view replacement) {
    const auto found = text.find(pattern);
    if (found == std::string_view::npos) {
      return std::string(text);
    }
    const auto after = text.substr(found + pattern.size());
    return std::string(text.substr(0, found)) + expandReplacement(replacement, pattern, text.substr(0, found), after) +
           std::string(after);
  }

  // ── Regular expressions ────────────────────────────────────────────────────

  Regex::Regex(std::string_view pattern, std::string_view flags) : m_empty(pattern.empty()) {
    auto syntax = std::regex::ECMAScript;
    for (const char flag : flags) {
      if (flag == 'g') {
        m_global = true;
      } else if (flag == 'i') {
        syntax |= std::regex::icase;
      } else if (flag == 'm') {
        syntax |= std::regex::multiline;
      }
    }
    m_re = std::regex(pattern.begin(), pattern.end(), syntax);
  }

  namespace {

    // Byte length of the code point at text[i] (1 for a malformed byte).
    std::size_t codePointLength(std::string_view text, std::size_t i) {
      std::size_t next = i;
      decode(text, next);
      return std::max<std::size_t>(next - i, 1);
    }

    // Successive matches as RegExp.prototype.exec finds them with lastIndex: after an empty
    // match, the search resumes one character later (std::regex_iterator retries non-empty).
    template <typename F> void forEachMatch(std::string_view text, const Regex& re, bool all, F&& fn) {
      std::size_t pos = 0;
      std::cmatch m;
      while (pos <= text.size()) {
        if (!std::regex_search(text.data() + pos, text.data() + text.size(), m, re.re())) {
          return;
        }
        const std::size_t start = pos + static_cast<std::size_t>(m.position(0));
        const std::size_t end = start + static_cast<std::size_t>(m.length(0));
        fn(m, start, end);
        if (!all) {
          return;
        }
        pos = end == start ? end + (end < text.size() ? codePointLength(text, end) : 1) : end;
      }
    }

    // GetSubstitution: $$, $&, $`, $', $n and $nn (when there is such a group; else literal).
    std::string substitute(std::string_view replacement, const std::cmatch& m, std::string_view text, std::size_t start,
                           std::size_t end) {
      std::string out;
      const std::size_t groupCount = m.size() - 1;
      for (std::size_t i = 0; i < replacement.size(); ++i) {
        const char c = replacement[i];
        if (c != '$' || i + 1 >= replacement.size()) {
          out += c;
          continue;
        }
        const char next = replacement[i + 1];
        if (next == '$') {
          out += '$', ++i;
        } else if (next == '&') {
          out += text.substr(start, end - start), ++i;
        } else if (next == '`') {
          out += text.substr(0, start), ++i;
        } else if (next == '\'') {
          out += text.substr(end), ++i;
        } else if (next >= '0' && next <= '9') {
          std::size_t n = static_cast<std::size_t>(next - '0');
          std::size_t used = 1;
          if (i + 2 < replacement.size() && replacement[i + 2] >= '0' && replacement[i + 2] <= '9') {
            const std::size_t two = n * 10 + static_cast<std::size_t>(replacement[i + 2] - '0');
            if (two >= 1 && two <= groupCount) {
              n = two, used = 2;
            }
          }
          if (n >= 1 && n <= groupCount) {
            out += m[static_cast<int>(n)].matched ? m[static_cast<int>(n)].str() : std::string();
            i += used;
          } else {
            out += '$';
          }
        } else {
          out += '$';
        }
      }
      return out;
    }

  } // namespace

  bool test(const Regex& re, std::string_view text) { return std::regex_search(text.begin(), text.end(), re.re()); }

  int search(std::string_view text, const Regex& re) {
    std::cmatch m;
    if (!std::regex_search(text.begin(), text.end(), m, re.re())) {
      return -1;
    }
    return length(text.substr(0, static_cast<std::size_t>(m.position(0))));
  }

  std::optional<std::vector<std::string>> match(std::string_view text, const Regex& re) {
    std::vector<std::string> out;
    bool found = false;
    forEachMatch(text, re, re.global(), [&](const std::cmatch& m, std::size_t, std::size_t) {
      found = true;
      if (re.global()) {
        out.push_back(m[0].str());
      } else {
        out = groups(m);
      }
    });
    if (!found) {
      return std::nullopt;
    }
    return out;
  }

  std::string replace(std::string_view text, const Regex& re, std::string_view replacement) {
    std::string out;
    std::size_t last = 0;
    forEachMatch(text, re, re.global(), [&](const std::cmatch& m, std::size_t start, std::size_t end) {
      out += text.substr(last, start - last);
      out += substitute(replacement, m, text, start, end);
      last = end;
    });
    out += text.substr(std::min(last, text.size()));
    return out;
  }

  std::string replace(std::string_view text, const Regex& re,
                      const std::function<std::string(const std::vector<std::string>&)>& fn) {
    std::string out;
    std::size_t last = 0;
    forEachMatch(text, re, re.global(), [&](const std::cmatch& m, std::size_t start, std::size_t end) {
      out += text.substr(last, start - last);
      out += fn(groups(m));
      last = end;
    });
    out += text.substr(std::min(last, text.size()));
    return out;
  }

  std::vector<std::string> split(std::string_view text, const Regex& separator, std::uint32_t limit) {
    // V4's String.prototype.split (qv4stringobject.cpp), not the spec's: it searches from the
    // offset, moves on by max(offset + 1, match end), and its limit check inside the capture
    // loop only leaves that loop. An empty pattern splits like "".
    std::vector<std::string> out;
    if (separator.empty()) {
      return split(text, std::string_view(), limit);
    }
    std::size_t offset = 0;
    std::cmatch m;
    while (offset <= text.size() &&
           std::regex_search(text.data() + offset, text.data() + text.size(), m, separator.re())) {
      const std::size_t start = offset + static_cast<std::size_t>(m.position(0));
      const std::size_t end = start + static_cast<std::size_t>(m.length(0));
      out.emplace_back(text.substr(offset, start - offset));
      offset = std::max(offset + (offset < text.size() ? codePointLength(text, offset) : 1), end);
      if (out.size() >= limit) {
        break;
      }
      for (std::size_t i = 1; i < m.size(); ++i) {
        out.push_back(m[i].matched ? m[i].str() : std::string());
        if (out.size() >= limit) {
          break;
        }
      }
    }
    if (out.size() < limit) {
      out.emplace_back(offset <= text.size() ? text.substr(offset) : std::string_view());
    }
    return out;
  }

  // ── Arrays ─────────────────────────────────────────────────────────────────

  std::string join(const std::vector<std::string>& array, std::string_view separator) {
    std::string out;
    for (std::size_t i = 0; i < array.size(); ++i) {
      if (i > 0) {
        out += separator;
      }
      out += array[i];
    }
    return out;
  }

  std::string join(const std::vector<double>& array, std::string_view separator) {
    std::string out;
    for (std::size_t i = 0; i < array.size(); ++i) {
      if (i > 0) {
        out += separator;
      }
      out += toString(array[i]);
    }
    return out;
  }

  std::string join(const std::vector<int>& array, std::string_view separator) {
    std::string out;
    for (std::size_t i = 0; i < array.size(); ++i) {
      if (i > 0) {
        out += separator;
      }
      out += std::to_string(array[i]);
    }
    return out;
  }

  std::vector<std::string> sorted(std::vector<std::string> array) {
    // V4 compares String(a) < String(b) as UTF-16; UTF-8 byte order is the same outside the
    // supplementary planes.
    detail::v4Sort(array.begin(), array.end(), [](const std::string& a, const std::string& b) { return a < b; });
    return array;
  }

  std::vector<double> sorted(std::vector<double> array) {
    detail::v4Sort(array.begin(), array.end(), [](double a, double b) { return toString(a) < toString(b); });
    return array;
  }

  // ── JSON ───────────────────────────────────────────────────────────────────

  Json parse(std::string_view text) {
    try {
      return Json::parse(text);
    } catch (const Json::parse_error& e) {
      throw std::invalid_argument(std::string("JSON.parse: ") + e.what());
    }
  }

  std::string stringify(const Json& value, int indent) {
    std::string out;
    stringifyTo(value, std::min(indent, 10), 0, out);  // JS caps the indent at 10
    return out;
  }

  // ── console ────────────────────────────────────────────────────────────────

  void logLine(int level, std::string_view message) {
    static constexpr Logger kLog("qml");
    switch (level) {
    case 0: kLog.info("{}", message); break;
    case 1: kLog.warn("{}", message); break;
    default: kLog.error("{}", message); break;
    }
  }

} // namespace ii::js
