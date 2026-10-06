#pragma once

// QML's point, size and rect value types (and vector2d, which ii only uses as a point).

namespace ii {

  struct Point {
    double x = 0.0;
    double y = 0.0;
    friend bool operator==(const Point&, const Point&) = default;
  };

  struct Size {
    double width = 0.0;
    double height = 0.0;
    friend bool operator==(const Size&, const Size&) = default;
  };

  struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    friend bool operator==(const Rect&, const Rect&) = default;
  };

} // namespace ii
