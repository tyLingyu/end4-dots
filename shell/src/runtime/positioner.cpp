#include "runtime/positioner.h"

#include <algorithm>

namespace ii {

  Positioner::Positioner(Axis orientation) : m_orientation(orientation) {
    leftPadding.bind([this] { return padding.get(); }, "Positioner.leftPadding (padding)");
    rightPadding.bind([this] { return padding.get(); }, "Positioner.rightPadding (padding)");
    topPadding.bind([this] { return padding.get(); }, "Positioner.topPadding (padding)");
    bottomPadding.bind([this] { return padding.get(); }, "Positioner.bottomPadding (padding)");

    const bool horizontal = orientation == Axis::Horizontal;
    implicitWidth.bind(
        [this, horizontal] {
          double extent = 0.0;
          const auto children = placed();
          for (const Item* child : children) {
            extent = horizontal ? extent + child->width.get() : std::max(extent, child->width.get());
          }
          if (horizontal && !children.empty()) {
            extent += spacing.get() * static_cast<double>(children.size() - 1);
          }
          return leftPadding.get() + extent + rightPadding.get();
        },
        "Positioner.implicitWidth"
    );
    implicitHeight.bind(
        [this, horizontal] {
          double extent = 0.0;
          const auto children = placed();
          for (const Item* child : children) {
            extent = horizontal ? std::max(extent, child->height.get()) : extent + child->height.get();
          }
          if (!horizontal && !children.empty()) {
            extent += spacing.get() * static_cast<double>(children.size() - 1);
          }
          return topPadding.get() + extent + bottomPadding.get();
        },
        "Positioner.implicitHeight"
    );
    m_tracker.bind(
        [this] {
          for (const Item* child : placed()) {
            (void)child->width.get();
            (void)child->height.get();
          }
          (void)spacing.get();
          (void)leftPadding.get();
          (void)topPadding.get();
          polishLater();
          return ++m_trackerCount;
        },
        "Positioner (reposition tracker)"
    );
  }

  std::vector<Item*> Positioner::placed() const {
    (void)childrenRevision.get();
    std::vector<Item*> list;
    for (Item* child : childItems()) {
      if (child->visible.get() && child->width.get() > 0.0 && child->height.get() > 0.0) {
        list.push_back(child);
      }
    }
    return list;
  }

  void Positioner::polish() {
    const bool horizontal = m_orientation == Axis::Horizontal;
    double position = horizontal ? leftPadding.peek() : topPadding.peek();
    for (Item* child : placed()) {
      if (horizontal) {
        child->x.set(position);
        child->y.set(topPadding.peek());
        position += child->width.peek() + spacing.peek();
      } else {
        child->x.set(leftPadding.peek());
        child->y.set(position);
        position += child->height.peek() + spacing.peek();
      }
    }
  }

} // namespace ii
