#include "runtime/effects.h"

#include "render/scene/shadow_node.h"

#include <algorithm>
#include <cmath>

namespace ii {

  RectangularShadow::RectangularShadow() {
    m_shadow = static_cast<ShadowNode*>(node()->addChild(std::make_unique<ShadowNode>()));
    for (PropertyBase* p : std::initializer_list<PropertyBase*>{&blur, &spread, &radius, &offset, &color}) {
      p->changed().connectForever([this] { update(); });
    }
    update();
  }

  double RectangularShadow::clampedRadius(double r, double b, double s) const {
    double maxRadius = std::min(width.peek(), height.peek()) * 0.5;
    maxRadius += s * 2.0;
    double spreadRadius = r + s;
    if (r < s && s != 0.0) {
      // CSS box-shadow: the spread is scaled by 1 + (ratio - 1)^3.
      const double q = (r / s) - 1.0;
      spreadRadius = r + s * (1.0 + q * q * q);
    }
    const double blurReduce = b * 0.75;
    maxRadius -= blurReduce;
    const double limitedRadius = std::max(0.0, spreadRadius - blurReduce);
    return std::min(limitedRadius, maxRadius);
  }

  void RectangularShadow::update() {
    // Qt clamps blur and radius to >= 0 when they are set.
    const double b = std::max(0.0, blur.peek());
    const double r = std::max(0.0, radius.peek());
    const double s = spread.peek();
    const double w = width.peek();
    const double h = height.peek();

    const double padding = b * 2.0 + s * 2.0;
    const double effectWidth = w + padding;
    const double effectHeight = h + padding;
    m_shadow->setPosition(static_cast<float>((w - effectWidth) * 0.5 + offset.peek().x),
                          static_cast<float>((h - effectHeight) * 0.5 + offset.peek().y));
    m_shadow->setSize(static_cast<float>(effectWidth), static_cast<float>(effectHeight));

    // Implicit antialiasing (one pixel) when the radius is positive, as in Qt.
    const double aa = r > 0.0 ? 1.0 : 0.0;
    const double blurReduction = b * 1.8 + aa;
    m_shadow->setStyle(ShadowStyle{
        .color = color.peek(),
        .rectWidth = static_cast<float>(effectWidth * 0.5 - blurReduction),
        .rectHeight = static_cast<float>(effectHeight * 0.5 - blurReduction),
        .radius = static_cast<float>(clampedRadius(r, b, s)),
        .blur = static_cast<float>(b * 2.1 + aa),
    });
  }

} // namespace ii
