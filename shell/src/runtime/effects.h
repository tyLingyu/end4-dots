#pragma once

#include "runtime/color.h"
#include "runtime/geometry.h"
#include "runtime/item.h"

class ShadowNode;

namespace ii {

  // QtQuick.Effects RectangularShadow: a blurred rounded rectangle of the item's size, drawn over
  // an area grown by 2 * blur + 2 * spread and moved by offset. The geometry and the radius
  // clamping are QQuickRectangularShadowPrivate's (Qt 6.11), the shader is Qt's.
  class RectangularShadow : public Item {
  public:
    RectangularShadow();

    Property<double> blur{10.0};
    Property<double> spread;
    Property<double> radius;
    Property<Point> offset;
    Property<Color> color{Color{0.0F, 0.0F, 0.0F, 1.0F}};
    Property<bool> cached;  // Qt may cache the shadow texture; it looks the same

  protected:
    void geometryChanged() override { update(); }

  private:
    void update();
    [[nodiscard]] double clampedRadius(double radius, double blur, double spread) const;

    ShadowNode* m_shadow = nullptr;
  };

} // namespace ii
