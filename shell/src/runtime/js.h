#pragma once

// JavaScript semantics that generated code (tools/qml2cpp) relies on.
//
// Translated JavaScript calls these instead of writing the C++ equivalent inline, so edge cases
// behave as in Qt's JS engine everywhere: string indices count UTF-16 code units, Number() and
// parseInt() accept exactly what JS accepts, toFixed() rounds ties up, sort() is stable with a
// numeric comparator, JSON.stringify() formats numbers as JS does. tests/diff/js_dump.qml
// samples Qt for the expected values (tests/runtime/js_test.cpp).
//
// The reference is Qt's JS engine (V4), which quickshell runs, not the ECMAScript spec: where V4
// differs (Math.round, sort order, regex split, "".indexOf("")) these follow V4. V4 also lacks
// trimStart, trimEnd and replaceAll, so ii never calls them.

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// A JavaScript expression qml2cpp could not translate. The translation belongs in
// tools/qml2cpp/data/js/<qml file>.json under this key; regenerating then replaces the stub.
#define II_TODO_JS(key) static_assert(false, "untranslated JavaScript " key " (tools/qml2cpp/data/js)")

namespace ii::js {

  // JSON values keep their keys in insertion order, as JavaScript objects do.
  using Json = nlohmann::ordered_json;

  inline constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

  // ── Numbers ────────────────────────────────────────────────────────────────

  // Number.prototype.toString() / String(number): shortest round-trip digits, fixed notation
  // for 1e-7 < |x| < 1e21, otherwise "1e+21" / "1.5e-7".
  [[nodiscard]] std::string toString(double value);
  // Number.prototype.toString(radix) for integers (2..36).
  [[nodiscard]] std::string toString(double value, int radix);
  // Number.prototype.toFixed(digits): exact decimal value, ties rounded away from zero.
  [[nodiscard]] std::string toFixed(double value, int digits = 0);
  // Number(string): whitespace-trimmed decimal/hex/octal/binary literal, "" -> 0, else NaN.
  [[nodiscard]] double toNumber(std::string_view text);
  [[nodiscard]] inline double toNumber(bool value) { return value ? 1.0 : 0.0; }
  [[nodiscard]] inline double toNumber(double value) { return value; }
  // parseInt / parseFloat: the longest valid prefix, NaN when there is none.
  [[nodiscard]] double parseInt(std::string_view text, int radix = 0);
  [[nodiscard]] double parseFloat(std::string_view text);

  // Math.round as V4 computes it: halves towards +Infinity (Math.round(-2.5) == -2), and
  // floor(v + 0.5) even where the addition rounds (4503599627370497 -> 4503599627370498).
  [[nodiscard]] inline double round(double value) {
    if (!std::isfinite(value) || value == 0.0) {
      return value;
    }
    if (value < 0.5 && value >= -0.5) {
      return std::copysign(0.0, value);
    }
    return std::floor(value + 0.5);
  }

  // ── Strings (UTF-8 in, UTF-8 out; positions and lengths in UTF-16 code units) ──

  [[nodiscard]] std::u16string toUtf16(std::string_view text);
  [[nodiscard]] std::string toUtf8(std::u16string_view text);

  // string.length
  [[nodiscard]] int length(std::string_view text);
  [[nodiscard]] std::string slice(std::string_view text, int start, int end = INT_MAX);
  [[nodiscard]] std::string substring(std::string_view text, int start, int end = INT_MAX);
  [[nodiscard]] std::string substr(std::string_view text, int start, int count = INT_MAX);
  [[nodiscard]] std::string charAt(std::string_view text, int index);
  [[nodiscard]] double charCodeAt(std::string_view text, int index);  // NaN when out of range
  [[nodiscard]] int indexOf(std::string_view text, std::string_view search, int from = 0);
  [[nodiscard]] int lastIndexOf(std::string_view text, std::string_view search, int from = INT_MAX);
  [[nodiscard]] bool includes(std::string_view text, std::string_view search, int from = 0);
  [[nodiscard]] bool startsWith(std::string_view text, std::string_view search, int position = 0);
  [[nodiscard]] bool endsWith(std::string_view text, std::string_view search, int endPosition = INT_MAX);
  // JS whitespace: Unicode Zs, tab, VT, FF, NBSP, BOM and the line terminators.
  [[nodiscard]] std::string trim(std::string_view text);
  [[nodiscard]] std::string toLowerCase(std::string_view text);
  [[nodiscard]] std::string toUpperCase(std::string_view text);
  [[nodiscard]] std::string padStart(std::string_view text, int targetLength, std::string_view fill = " ");
  [[nodiscard]] std::string padEnd(std::string_view text, int targetLength, std::string_view fill = " ");
  [[nodiscard]] std::string repeat(std::string_view text, int count);
  // split with a string separator; "" splits into UTF-16 code units.
  [[nodiscard]] std::vector<std::string> split(std::string_view text, std::string_view separator,
                                               std::uint32_t limit = UINT32_MAX);
  // replace with a string pattern: the first occurrence only; `$&`, `$$`, `` $` ``, `$'` expand.
  [[nodiscard]] std::string replace(std::string_view text, std::string_view pattern, std::string_view replacement);

  // ── Regular expressions: /pattern/flags -> Regex("pattern", "flags") ──────────

  // JS regular expressions through std::wregex over UTF-32 text, with the pattern rewritten where
  // std's ECMAScript differs from V4 (\s and . as JS defines them, literal `{`). Flags: g (global),
  // i (ignore case), m (multiline). Not supported: the u and y flags, lookbehind, named groups.
  class Regex {
  public:
    Regex(std::string_view pattern, std::string_view flags = {});

    [[nodiscard]] const std::wregex& re() const noexcept { return m_re; }  // over UTF-32 text
    [[nodiscard]] bool global() const noexcept { return m_global; }
    [[nodiscard]] bool empty() const noexcept { return m_empty; }

  private:
    std::wregex m_re;
    bool m_global = false;
    bool m_empty = false;
  };

  // A match: its UTF-16 index and [match, groups...] (unmatched groups "").
  struct RegexMatch {
    int index = 0;
    std::vector<std::string> groups;
  };

  // re.test(text) / text.search(re) (UTF-16 index, -1 when none)
  [[nodiscard]] bool test(const Regex& re, std::string_view text);
  [[nodiscard]] int search(std::string_view text, const Regex& re);
  // re.exec(text), with the regex's lastIndex kept by the caller (UTF-16 units): a global regex
  // searches from lastIndex and moves it past the match, or back to 0 when there is none.
  [[nodiscard]] std::optional<RegexMatch> exec(const Regex& re, std::string_view text, int& lastIndex);
  // text.match(re): without g, [match, groups...] (unmatched groups ""); with g, every match.
  // nullopt where JS returns null.
  [[nodiscard]] std::optional<std::vector<std::string>> match(std::string_view text, const Regex& re);
  // text.replace(re, replacement): the first match, or every match with g; `$1`, `$&` expand.
  [[nodiscard]] std::string replace(std::string_view text, const Regex& re, std::string_view replacement);
  // text.replace(re, fn): fn receives [match, groups...] and returns the replacement.
  [[nodiscard]] std::string replace(std::string_view text, const Regex& re,
                                    const std::function<std::string(const std::vector<std::string>&)>& fn);
  [[nodiscard]] std::vector<std::string> split(std::string_view text, const Regex& separator,
                                               std::uint32_t limit = UINT32_MAX);

  // ── Arrays (std::vector) ───────────────────────────────────────────────────

  namespace detail {
    // Relative index as Array.prototype.slice resolves it.
    [[nodiscard]] inline std::size_t relative(int index, std::size_t size) {
      const auto n = static_cast<long long>(size);
      const long long i = index < 0 ? std::max(0LL, n + index) : std::min<long long>(index, n);
      return static_cast<std::size_t>(i);
    }
  } // namespace detail

  template <typename T, typename U> [[nodiscard]] int indexOf(const std::vector<T>& array, const U& value) {
    const auto it = std::find(array.begin(), array.end(), value);
    return it == array.end() ? -1 : static_cast<int>(it - array.begin());
  }

  template <typename T, typename U> [[nodiscard]] bool includes(const std::vector<T>& array, const U& value) {
    return std::find(array.begin(), array.end(), value) != array.end();
  }

  template <typename T> [[nodiscard]] std::vector<T> slice(const std::vector<T>& array, int start = 0, int end = INT_MAX) {
    const std::size_t from = detail::relative(start, array.size());
    const std::size_t to = detail::relative(end, array.size());
    return from < to ? std::vector<T>(array.begin() + from, array.begin() + to) : std::vector<T>{};
  }

  // array.map(fn): fn(element) or fn(element, index).
  template <typename T, typename F> [[nodiscard]] auto map(const std::vector<T>& array, F&& fn) {
    if constexpr (std::is_invocable_v<F&, const T&, int>) {
      std::vector<std::decay_t<std::invoke_result_t<F&, const T&, int>>> out;
      out.reserve(array.size());
      for (std::size_t i = 0; i < array.size(); ++i) {
        out.push_back(fn(array[i], static_cast<int>(i)));
      }
      return out;
    } else {
      std::vector<std::decay_t<std::invoke_result_t<F&, const T&>>> out;
      out.reserve(array.size());
      for (const T& element : array) {
        out.push_back(fn(element));
      }
      return out;
    }
  }

  template <typename T, typename F> [[nodiscard]] std::vector<T> filter(const std::vector<T>& array, F&& fn) {
    std::vector<T> out;
    for (const T& element : array) {
      if (fn(element)) {
        out.push_back(element);
      }
    }
    return out;
  }

  // array.find(fn): nullopt where JS returns undefined.
  template <typename T, typename F> [[nodiscard]] std::optional<T> find(const std::vector<T>& array, F&& fn) {
    for (const T& element : array) {
      if (fn(element)) {
        return element;
      }
    }
    return std::nullopt;
  }

  template <typename T, typename F> [[nodiscard]] int findIndex(const std::vector<T>& array, F&& fn) {
    for (std::size_t i = 0; i < array.size(); ++i) {
      if (fn(array[i])) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  template <typename T, typename F> [[nodiscard]] bool some(const std::vector<T>& array, F&& fn) {
    return std::any_of(array.begin(), array.end(), fn);
  }

  template <typename T, typename F> [[nodiscard]] bool every(const std::vector<T>& array, F&& fn) {
    return std::all_of(array.begin(), array.end(), fn);
  }

  namespace detail {
    // V4's sort (sortHelper in Qt's qv4arraydata_p.h, Qt 6.11, GPL-3.0-only), so equal elements
    // end up in the order quickshell gives them: the sort is not stable.
    template <typename Iterator, typename LessThan> void v4Sort(Iterator start, Iterator end, LessThan lessThan) {
      using std::swap;
      while (true) {
        const int span = static_cast<int>(end - start);
        if (span < 2) {
          return;
        }
        --end;
        Iterator low = start;
        Iterator high = end - 1;
        Iterator pivot = start + span / 2;
        if (lessThan(*end, *start)) {
          swap(*end, *start);
        }
        if (span == 2) {
          return;
        }
        if (lessThan(*pivot, *start)) {
          swap(*pivot, *start);
        }
        if (lessThan(*end, *pivot)) {
          swap(*end, *pivot);
        }
        if (span == 3) {
          return;
        }
        swap(*pivot, *end);
        while (low < high) {
          while (low < high && lessThan(*low, *end)) {
            ++low;
          }
          while (high > low && lessThan(*end, *high)) {
            --high;
          }
          if (low < high) {
            swap(*low, *high);
            ++low;
            --high;
          } else {
            break;
          }
        }
        if (lessThan(*low, *end)) {
          ++low;
        }
        swap(*end, *low);
        v4Sort(start, low, lessThan);
        start = low + 1;
        ++end;
      }
    }
  } // namespace detail

  // array.sort(cmp) on a copy: cmp returns a number, < 0 puts a first. Not stable (as in V4).
  template <typename T, typename F> [[nodiscard]] std::vector<T> sorted(std::vector<T> array, F&& cmp) {
    detail::v4Sort(array.begin(), array.end(), [&](const T& a, const T& b) { return cmp(a, b) < 0; });
    return array;
  }

  // array.sort() without a comparator orders by String(element).
  [[nodiscard]] std::vector<std::string> sorted(std::vector<std::string> array);
  [[nodiscard]] std::vector<double> sorted(std::vector<double> array);

  // array.join(separator): elements converted as String() does.
  [[nodiscard]] std::string join(const std::vector<std::string>& array, std::string_view separator = ",");
  [[nodiscard]] std::string join(const std::vector<double>& array, std::string_view separator = ",");
  [[nodiscard]] std::string join(const std::vector<int>& array, std::string_view separator = ",");

  // ── JSON ───────────────────────────────────────────────────────────────────

  // JSON.parse: throws std::invalid_argument (a JS SyntaxError) on malformed text.
  [[nodiscard]] Json parse(std::string_view text);
  // JSON.stringify(value, null, indent): JS number formatting, key order kept; indent <= 0 is compact.
  [[nodiscard]] std::string stringify(const Json& value, int indent = 0);

  // Date.now(): milliseconds since the epoch.
  [[nodiscard]] double dateNow();

  // ── console ────────────────────────────────────────────────────────────────

  // console.log(a, b, ...) joins its arguments with spaces.
  [[nodiscard]] inline std::string logString(std::string_view value) { return std::string(value); }
  [[nodiscard]] inline std::string logString(const char* value) { return value; }
  [[nodiscard]] inline std::string logString(const std::string& value) { return value; }
  [[nodiscard]] inline std::string logString(bool value) { return value ? "true" : "false"; }
  [[nodiscard]] inline std::string logString(double value) { return toString(value); }
  [[nodiscard]] inline std::string logString(int value) { return std::to_string(value); }
  [[nodiscard]] inline std::string logString(const Json& value) {
    return value.is_string() ? value.get<std::string>() : stringify(value);
  }
  [[nodiscard]] inline std::string logString(const nlohmann::json& value) { return logString(Json(value)); }

  void logLine(int level, std::string_view message);

  template <typename... Args> void log(const Args&... args) {
    std::string line;
    ((line += (line.empty() ? "" : " ") + logString(args)), ...);
    logLine(0, line);
  }
  template <typename... Args> void warn(const Args&... args) {
    std::string line;
    ((line += (line.empty() ? "" : " ") + logString(args)), ...);
    logLine(1, line);
  }
  template <typename... Args> void error(const Args&... args) {
    std::string line;
    ((line += (line.empty() ? "" : " ") + logString(args)), ...);
    logLine(2, line);
  }

} // namespace ii::js

namespace ii {

  // Names generated code uses directly (qml2cpp's mechanical translation).
  [[nodiscard]] inline double jsRound(double value) { return js::round(value); }
  [[nodiscard]] inline std::string jsString(double value) { return js::toString(value); }
  [[nodiscard]] inline std::string jsString(int value) { return std::to_string(value); }

} // namespace ii
