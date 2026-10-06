#pragma once

#include <cstdint>
#include <vector>

namespace ii {

  // QEasingCurve, as QML animations use it. Formulas are Qt's own variants of Penner's
  // equations (e.g. OutExpo's 1.001 factor); calibrated in tests/diff/expected/easing.json.
  struct Easing {
    enum class Type : std::uint8_t {
      Linear,
      InQuad, OutQuad, InOutQuad,
      InCubic, OutCubic, InOutCubic,
      InSine, OutSine, InOutSine,
      InExpo, OutExpo, InOutExpo,
      OutBack,
      BezierSpline,
    };

    Type type = Type::Linear;
    // BezierSpline: groups of (c1x, c1y, c2x, c2y, endX, endY); the first segment starts at (0, 0).
    // A list whose length isn't a multiple of 6 is ignored (Qt silently keeps no curve: linear).
    std::vector<double> bezierCurve;
    double overshoot = 1.70158;

    [[nodiscard]] double value(double progress) const;

    friend bool operator==(const Easing&, const Easing&) = default;
  };

  // QEasingCurve::Type values (`Easing.BezierSpline` is 45 in QML) to the types implemented here;
  // unknown values fall back to Linear.
  [[nodiscard]] Easing::Type easingTypeFromQt(int qtType);

} // namespace ii
