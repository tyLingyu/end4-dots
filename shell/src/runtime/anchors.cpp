#include "runtime/anchors.h"

#include "core/log.h"
#include "runtime/item.h"

#include <cmath>

namespace ii {

  namespace {
    constexpr Logger kLog("anchors");

    // Position of `line` in the coordinate system of `item`'s parent: the target is either that
    // parent (edges measured from 0) or a sibling (edges measured from its x/y).
    double edgePosition(const Item& item, const AnchorLine& line) {
      const Item& target = *line.item;
      const Item* parent = item.parent.get();
      const bool isParent = &target == parent;
      if (!isParent && target.parent.get() != parent) {
        kLog.warn("cannot anchor to an item that isn't a parent or sibling");
      }
      switch (line.edge) {
      case AnchorEdge::Left:
        return isParent ? 0.0 : target.x.get();
      case AnchorEdge::Right:
        return (isParent ? 0.0 : target.x.get()) + target.width.get();
      case AnchorEdge::HorizontalCenter:
        return (isParent ? 0.0 : target.x.get()) + Anchors::halfWidth(target);
      case AnchorEdge::Top:
        return isParent ? 0.0 : target.y.get();
      case AnchorEdge::Bottom:
        return (isParent ? 0.0 : target.y.get()) + target.height.get();
      case AnchorEdge::VerticalCenter:
        return (isParent ? 0.0 : target.y.get()) + Anchors::halfHeight(target);
      case AnchorEdge::None:
        break;
      }
      return 0.0;
    }

    double half(double extent, const Item& item) {
      const Anchors* anchors = item.anchorsIfAny();
      if (anchors != nullptr && !anchors->alignWhenCentered.get()) {
        return extent / 2.0;
      }
      return std::floor(extent / 2.0 + 0.5); // qRound, for the non-negative extents seen here
    }
  } // namespace

  Anchors::Anchors(Item& item) : m_item(item) {
    leftMargin.bind([this] { return margins.get(); }, "anchors.leftMargin (margins)");
    rightMargin.bind([this] { return margins.get(); }, "anchors.rightMargin (margins)");
    topMargin.bind([this] { return margins.get(); }, "anchors.topMargin (margins)");
    bottomMargin.bind([this] { return margins.get(); }, "anchors.bottomMargin (margins)");

    // Only the structural properties need hooks: margins, offsets, alignWhenCentered and the
    // targets' geometry are read inside the installed bindings and tracked there.
    fill.setHook(&onHorizontalChanged, this);
    fill.changed().connectForever([this] { updateVertical(); });
    centerIn.setHook(&onHorizontalChanged, this);
    centerIn.changed().connectForever([this] { updateVertical(); });
    left.setHook(&onHorizontalChanged, this);
    right.setHook(&onHorizontalChanged, this);
    horizontalCenter.setHook(&onHorizontalChanged, this);
    top.setHook(&onVerticalChanged, this);
    bottom.setHook(&onVerticalChanged, this);
    verticalCenter.setHook(&onVerticalChanged, this);
  }

  Anchors::~Anchors() = default;

  double Anchors::halfWidth(const Item& item) { return half(item.width.get(), item); }

  double Anchors::halfHeight(const Item& item) { return half(item.height.get(), item); }

  void Anchors::onHorizontalChanged(void* self) { static_cast<Anchors*>(self)->updateHorizontal(); }

  void Anchors::onVerticalChanged(void* self) { static_cast<Anchors*>(self)->updateVertical(); }

  void Anchors::updateHorizontal() {
    Item* fillTarget = fill.peek();
    Item* centerTarget = centerIn.peek();
    const AnchorLine l = fillTarget != nullptr ? AnchorLine{fillTarget, AnchorEdge::Left} : left.peek();
    const AnchorLine r = fillTarget != nullptr ? AnchorLine{fillTarget, AnchorEdge::Right} : right.peek();
    const AnchorLine c =
        centerTarget != nullptr ? AnchorLine{centerTarget, AnchorEdge::HorizontalCenter} : horizontalCenter.peek();

    Item& item = m_item;
    bool ownsX = true;
    bool ownsWidth = false;
    if (l.valid() && r.valid()) {
      item.x.bind([this, l] { return edgePosition(m_item, l) + leftMargin.get(); }, "anchors (x)");
      item.width.bind(
          [this, l, r] {
            return edgePosition(m_item, r) - rightMargin.get() - (edgePosition(m_item, l) + leftMargin.get());
          },
          "anchors (width)"
      );
      ownsWidth = true;
    } else if (l.valid()) {
      item.x.bind([this, l] { return edgePosition(m_item, l) + leftMargin.get(); }, "anchors (x)");
    } else if (r.valid()) {
      item.x.bind(
          [this, r] { return edgePosition(m_item, r) - rightMargin.get() - m_item.width.get(); }, "anchors (x)"
      );
    } else if (c.valid()) {
      item.x.bind(
          [this, c] { return edgePosition(m_item, c) - halfWidth(m_item) + horizontalCenterOffset.get(); },
          "anchors (x)"
      );
    } else {
      ownsX = false;
    }

    // Release what this object installed and no longer needs. An anchored position keeps its
    // last value, like QML; an anchored size goes back to following the implicit size.
    if (!ownsX && m_xBinding != nullptr && item.x.binding() == m_xBinding) {
      item.x.clearBinding();
    }
    if (!ownsWidth && m_widthBinding != nullptr && item.width.binding() == m_widthBinding) {
      item.width.bind([&item] { return item.implicitWidth.get(); }, kImplicitWidthBindingName);
    }
    m_xBinding = ownsX ? item.x.binding() : nullptr;
    m_widthBinding = ownsWidth ? item.width.binding() : nullptr;
  }

  void Anchors::updateVertical() {
    Item* fillTarget = fill.peek();
    Item* centerTarget = centerIn.peek();
    const AnchorLine t = fillTarget != nullptr ? AnchorLine{fillTarget, AnchorEdge::Top} : top.peek();
    const AnchorLine b = fillTarget != nullptr ? AnchorLine{fillTarget, AnchorEdge::Bottom} : bottom.peek();
    const AnchorLine c =
        centerTarget != nullptr ? AnchorLine{centerTarget, AnchorEdge::VerticalCenter} : verticalCenter.peek();

    Item& item = m_item;
    bool ownsY = true;
    bool ownsHeight = false;
    if (t.valid() && b.valid()) {
      item.y.bind([this, t] { return edgePosition(m_item, t) + topMargin.get(); }, "anchors (y)");
      item.height.bind(
          [this, t, b] {
            return edgePosition(m_item, b) - bottomMargin.get() - (edgePosition(m_item, t) + topMargin.get());
          },
          "anchors (height)"
      );
      ownsHeight = true;
    } else if (t.valid()) {
      item.y.bind([this, t] { return edgePosition(m_item, t) + topMargin.get(); }, "anchors (y)");
    } else if (b.valid()) {
      item.y.bind(
          [this, b] { return edgePosition(m_item, b) - bottomMargin.get() - m_item.height.get(); }, "anchors (y)"
      );
    } else if (c.valid()) {
      item.y.bind(
          [this, c] { return edgePosition(m_item, c) - halfHeight(m_item) + verticalCenterOffset.get(); },
          "anchors (y)"
      );
    } else {
      ownsY = false;
    }

    // Release what this object installed and no longer needs. An anchored position keeps its
    // last value, like QML; an anchored size goes back to following the implicit size.
    if (!ownsY && m_yBinding != nullptr && item.y.binding() == m_yBinding) {
      item.y.clearBinding();
    }
    if (!ownsHeight && m_heightBinding != nullptr && item.height.binding() == m_heightBinding) {
      item.height.bind([&item] { return item.implicitHeight.get(); }, kImplicitHeightBindingName);
    }
    m_yBinding = ownsY ? item.y.binding() : nullptr;
    m_heightBinding = ownsHeight ? item.height.binding() : nullptr;
  }

} // namespace ii
