#include "runtime/rectangle.h"

#include "render/scene/rect_node.h"

#include <algorithm>

namespace ii {

  Rectangle::Rectangle() : Item(std::make_unique<RectNode>()) {
    color.setHook(&syncStyle, this);
    radius.setHook(&syncStyle, this);
    border.width.setHook(&syncStyle, this);
    border.color.setHook(&syncStyle, this);
    syncStyle(this);
  }

  RectNode& Rectangle::rectNode() const noexcept { return *static_cast<RectNode*>(node()); }

  void Rectangle::geometryChanged() { syncStyle(this); }

  void Rectangle::syncStyle(void* self) {
    auto* rect = static_cast<Rectangle*>(self);
    RoundedRectStyle style;
    style.fill = rect->color.peek();
    // QQuickRectangle clamps the radius to half the shorter side.
    const double maxRadius = std::min(rect->width.peek(), rect->height.peek()) / 2.0;
    style.radius = Radii(static_cast<float>(std::clamp(rect->radius.peek(), 0.0, std::max(maxRadius, 0.0))));
    style.borderWidth = static_cast<float>(std::max(rect->border.width.peek(), 0.0));
    style.border = rect->border.color.peek();
    rect->rectNode().setStyle(style);
  }

} // namespace ii
