// Easing curves against values sampled from Qt (tests/diff/easing_dump.qml).

#include "runtime/easing.h"

#include "../check.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>

using ii::Easing;

namespace {

  nlohmann::json expected() {
    std::ifstream in(std::string(II_DIFF_EXPECTED_DIR) + "/easing.json");
    return nlohmann::json::parse(in);
  }

  const std::map<std::string, Easing::Type>& types() {
    using T = Easing::Type;
    static const std::map<std::string, T> all{
        {"Linear", T::Linear},       {"InQuad", T::InQuad},       {"OutQuad", T::OutQuad},
        {"InOutQuad", T::InOutQuad}, {"InCubic", T::InCubic},     {"OutCubic", T::OutCubic},
        {"InOutCubic", T::InOutCubic}, {"InSine", T::InSine},     {"OutSine", T::OutSine},
        {"InOutSine", T::InOutSine}, {"InExpo", T::InExpo},       {"OutExpo", T::OutExpo},
        {"InOutExpo", T::InOutExpo}, {"OutBack", T::OutBack},
    };
    return all;
  }

  // ii's curves, as written in modules/common/Appearance.qml.
  const std::map<std::string, std::vector<double>>& curves() {
    static const std::map<std::string, std::vector<double>> all{
        {"expressiveFastSpatial", {0.42, 1.67, 0.21, 0.90, 1, 1}},
        {"expressiveDefaultSpatial", {0.38, 1.21, 0.22, 1.00, 1, 1}},
        {"expressiveSlowSpatial", {0.39, 1.29, 0.35, 0.98, 1, 1}},
        {"expressiveEffects", {0.34, 0.80, 0.34, 1.00, 1, 1}},
        {"emphasized", {0.05, 0, 2.0 / 15, 0.06, 1.0 / 6, 0.4, 5.0 / 24, 0.82, 0.25, 1, 1, 1}},
        {"emphasizedLastHalf", {5.0 / 24, 0.82, 0.25, 1, 1, 1}},
        {"emphasizedAccel", {0.3, 0, 0.8, 0.15, 1, 1}},
        {"emphasizedDecel", {0.05, 0.7, 0.1, 1, 1, 1}},
        {"standard", {0.2, 0, 0, 1, 1, 1}},
        {"standardAccel", {0.3, 0, 1, 1, 1, 1}},
        {"standardDecel", {0, 0, 0, 1, 1, 1}},
    };
    return all;
  }

  // Largest |ours - Qt| over the 41 samples.
  double maxError(const Easing& easing, const nlohmann::json& samples) {
    double worst = 0.0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
      const double t = static_cast<double>(i) / 40.0;
      worst = std::max(worst, std::fabs(easing.value(t) - samples[i].get<double>()));
    }
    return worst;
  }

} // namespace

TEST("easing: named types match Qt") {
  const auto data = expected();
  for (const auto& [name, type] : types()) {
    const double error = maxError(Easing{.type = type}, data[name]);
    std::fprintf(stderr, "  %-12s max error %.2e\n", name.c_str(), error);
    CHECK(error < 1e-6);
  }
}

TEST("easing: ii's bezier curves match Qt") {
  const auto data = expected();
  for (const auto& [name, points] : curves()) {
    const double error = maxError(Easing{.type = Easing::Type::BezierSpline, .bezierCurve = points}, data["bezier:" + name]);
    std::fprintf(stderr, "  %-26s max error %.2e\n", name.c_str(), error);
    CHECK(error < 1e-3);
  }
}

TEST("easing: a malformed bezier list is ignored (linear), like a fresh Qt animation") {
  const auto data = expected();
  const Easing firstHalf{
      .type = Easing::Type::BezierSpline, .bezierCurve = {0.05, 0, 2.0 / 15, 0.06, 1.0 / 6, 0.4, 5.0 / 24, 0.82}
  };
  CHECK(maxError(firstHalf, data["fresh:emphasizedFirstHalf"]) < 1e-9);
}

TEST_MAIN()
