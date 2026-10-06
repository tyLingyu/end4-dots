#pragma once

// ii-shell: the node of QtQuick.Effects' RectangularShadow. The node covers the effect area; the
// style holds what Qt's shader receives (see programs/shadow_program.cpp).

#include "render/core/color.h"
#include "render/scene/node.h"

struct ShadowStyle {
  Color color;         // premultiplied by the program
  float rectWidth = 0.0F;   // half-extents of the casting rectangle (Qt's rectSize)
  float rectHeight = 0.0F;
  float radius = 0.0F;
  float blur = 0.0F;
  bool operator==(const ShadowStyle&) const = default;
};

class ShadowNode : public Node {
public:
  ShadowNode() : Node(NodeType::Shadow) {}

  [[nodiscard]] const ShadowStyle& style() const noexcept { return m_style; }

  void setStyle(const ShadowStyle& style) {
    if (m_style == style) {
      return;
    }
    m_style = style;
    markPaintDirty();
  }

private:
  ShadowStyle m_style;
};
