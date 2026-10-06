#pragma once

// Qt's JSON as Quickshell's JsonAdapter writes and reads it: QJsonDocument::toJson(Indented)
// formatting, QJsonValue::fromVariant for property values, QVariant::convert when reading.
// tests/runtime/qt_json_test.cpp checks the formatting against Qt's own output.

#include "runtime/color.h"
#include "runtime/js.h"

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace ii::qs {

  // QJsonDocument::toJson(QJsonDocument::Indented): 4-space indent, keys sorted, integral
  // doubles up to 2^53 as integers, other numbers as QByteArray::number(d, 'g', shortest).
  [[nodiscard]] std::string qtJsonIndented(const js::Json& json);

  // Every overload is declared up front: the container templates call the others, and argument-
  // dependent lookup won't find them here (they live in ii::qs, the values' types elsewhere).
  template <typename T> js::Json toQtJson(const std::vector<T>& values);
  template <typename T> js::Json toQtJson(const std::map<std::string, T>& values);
  template <typename T> js::Json toQtJson(const std::optional<T>& value);
  template <typename T>
    requires requires(js::Json& j, const T& v) { to_json(j, v); }
  js::Json toQtJson(const T& value);
  template <typename T> bool fromQtJson(const js::Json& json, std::vector<T>& out);
  template <typename T> bool fromQtJson(const js::Json& json, std::map<std::string, T>& out);
  template <typename T> bool fromQtJson(const js::Json& json, std::optional<T>& out);
  template <typename T>
    requires requires(const js::Json& j, T& v) { from_json(j, v); }
  bool fromQtJson(const js::Json& json, T& out);

  // ── property value -> JSON (QJsonValue::fromVariant) ─────────────────────────

  inline js::Json toQtJson(bool value) { return value; }
  inline js::Json toQtJson(int value) { return value; }
  inline js::Json toQtJson(double value) { return std::isfinite(value) ? js::Json(value) : js::Json(nullptr); }
  inline js::Json toQtJson(const std::string& value) { return value; }
  inline js::Json toQtJson(const js::Json& value) { return value; }
  // QColor converts to its name: #rrggbb, or #aarrggbb when not opaque.
  js::Json toQtJson(const Color& value);
  template <typename T> js::Json toQtJson(const std::vector<T>& values) {
    js::Json out = js::Json::array();
    for (const auto& v : values) {
      out.push_back(toQtJson(v));
    }
    return out;
  }
  template <typename T> js::Json toQtJson(const std::map<std::string, T>& values) {
    js::Json out = js::Json::object();
    for (const auto& [k, v] : values) {
      out[k] = toQtJson(v);
    }
    return out;
  }
  template <typename T> js::Json toQtJson(const std::optional<T>& value) {
    return value ? toQtJson(*value) : js::Json(nullptr);
  }
  // A var-types struct: through its to_json.
  template <typename T>
    requires requires(js::Json& j, const T& v) { to_json(j, v); }
  js::Json toQtJson(const T& value) {
    js::Json out;
    to_json(out, value);
    return out;
  }

  // ── JSON -> property value (QVariant::convert); false when Qt's conversion fails ──

  bool fromQtJson(const js::Json& json, bool& out);
  bool fromQtJson(const js::Json& json, int& out);
  bool fromQtJson(const js::Json& json, double& out);
  bool fromQtJson(const js::Json& json, std::string& out);
  bool fromQtJson(const js::Json& json, Color& out);
  inline bool fromQtJson(const js::Json& json, js::Json& out) {
    out = json;
    return true;
  }
  template <typename T> bool fromQtJson(const js::Json& json, std::vector<T>& out) {
    if (!json.is_array()) {
      return false;
    }
    std::vector<T> values;
    for (const auto& item : json) {
      T value{};
      if (!fromQtJson(item, value)) {
        return false;
      }
      values.push_back(std::move(value));
    }
    out = std::move(values);
    return true;
  }
  template <typename T> bool fromQtJson(const js::Json& json, std::map<std::string, T>& out) {
    if (!json.is_object()) {
      return false;
    }
    std::map<std::string, T> values;
    for (const auto& [k, item] : json.items()) {
      if (!fromQtJson(item, values[k])) {
        return false;
      }
    }
    out = std::move(values);
    return true;
  }
  template <typename T> bool fromQtJson(const js::Json& json, std::optional<T>& out) {
    if (json.is_null()) {
      out.reset();
      return true;
    }
    T value{};
    if (!fromQtJson(json, value)) {
      return false;
    }
    out = std::move(value);
    return true;
  }
  template <typename T>
    requires requires(const js::Json& j, T& v) { from_json(j, v); }
  bool fromQtJson(const js::Json& json, T& out) {
    try {
      from_json(json, out);
      return true;
    } catch (const std::exception&) {
      return false;
    }
  }

} // namespace ii::qs
