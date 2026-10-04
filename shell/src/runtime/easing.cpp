#include "runtime/easing.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ii {

  namespace {
    constexpr double kHalfPi = std::numbers::pi / 2.0;

    double bezier(double p0, double p1, double p2, double p3, double s) {
      const double u = 1.0 - s;
      return u * u * u * p0 + 3.0 * u * u * s * p1 + 3.0 * u * s * s * p2 + s * s * s * p3;
    }

    double bezierDerivative(double p0, double p1, double p2, double p3, double s) {
      const double u = 1.0 - s;
      return 3.0 * u * u * (p1 - p0) + 6.0 * u * s * (p2 - p1) + 3.0 * s * s * (p3 - p2);
    }

    // y on the spline at x, solving x(s) = x per segment (Newton, falling back to bisection).
    double splineValue(const std::vector<double>& points, double x) {
      double x0 = 0.0;
      double y0 = 0.0;
      for (std::size_t i = 0; i + 5 < points.size(); i += 6) {
        const double x1 = points[i];
        const double y1 = points[i + 1];
        const double x2 = points[i + 2];
        const double y2 = points[i + 3];
        const double x3 = points[i + 4];
        const double y3 = points[i + 5];
        const bool last = i + 6 >= points.size();
        if (x <= x3 || last) {
          double lo = 0.0;
          double hi = 1.0;
          double s = std::clamp((x - x0) / std::max(x3 - x0, 1e-12), 0.0, 1.0);
          for (int iteration = 0; iteration < 64; ++iteration) {
            const double error = bezier(x0, x1, x2, x3, s) - x;
            if (std::fabs(error) < 1e-12) {
              break;
            }
            if (error > 0.0) {
              hi = s;
            } else {
              lo = s;
            }
            const double slope = bezierDerivative(x0, x1, x2, x3, s);
            const double next = slope != 0.0 ? s - error / slope : (lo + hi) / 2.0;
            s = (next > lo && next < hi) ? next : (lo + hi) / 2.0;
          }
          return bezier(y0, y1, y2, y3, s);
        }
        x0 = x3;
        y0 = y3;
      }
      return x;
    }
  } // namespace

  double Easing::value(double t) const {
    switch (type) {
    case Type::Linear:
      return t;
    case Type::InQuad:
      return t * t;
    case Type::OutQuad:
      return -t * (t - 2.0);
    case Type::InOutQuad:
      t *= 2.0;
      if (t < 1.0) {
        return t * t / 2.0;
      }
      t -= 1.0;
      return -0.5 * (t * (t - 2.0) - 1.0);
    case Type::InCubic:
      return t * t * t;
    case Type::OutCubic:
      t -= 1.0;
      return t * t * t + 1.0;
    case Type::InOutCubic:
      t *= 2.0;
      if (t < 1.0) {
        return 0.5 * t * t * t;
      }
      t -= 2.0;
      return 0.5 * (t * t * t + 2.0);
    case Type::InSine:
      return t == 1.0 ? 1.0 : -std::cos(t * kHalfPi) + 1.0;
    case Type::OutSine:
      return std::sin(t * kHalfPi);
    case Type::InOutSine:
      return -0.5 * (std::cos(std::numbers::pi * t) - 1.0);
    case Type::InExpo:
      return (t == 0.0 || t == 1.0) ? t : std::pow(2.0, 10.0 * (t - 1.0)) - 0.001;
    case Type::OutExpo:
      return t == 1.0 ? 1.0 : 1.001 * (-std::pow(2.0, -10.0 * t) + 1.0);
    case Type::InOutExpo:
      if (t == 0.0 || t == 1.0) {
        return t;
      }
      t *= 2.0;
      if (t < 1.0) {
        return 0.5 * std::pow(2.0, 10.0 * (t - 1.0)) - 0.0005;
      }
      return 0.5 * 1.0005 * (-std::pow(2.0, -10.0 * (t - 1.0)) + 2.0);
    case Type::OutBack:
      t -= 1.0;
      return t * t * ((overshoot + 1.0) * t + overshoot) + 1.0;
    case Type::BezierSpline:
      if (bezierCurve.empty() || bezierCurve.size() % 6 != 0) {
        return t;
      }
      return splineValue(bezierCurve, t);
    }
    return t;
  }

} // namespace ii
