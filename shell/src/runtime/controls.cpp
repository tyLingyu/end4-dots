#include "runtime/controls.h"

#include <algorithm>
#include <cmath>

namespace ii {

  Control::Control() {
    // `padding` is the default for the four sides (QQuickControl: an unset side follows it).
    leftPadding.bind([this] { return padding.get(); }, "Control.leftPadding");
    rightPadding.bind([this] { return padding.get(); }, "Control.rightPadding");
    topPadding.bind([this] { return padding.get(); }, "Control.topPadding");
    bottomPadding.bind([this] { return padding.get(); }, "Control.bottomPadding");
    availableWidth.bind([this] { return std::max(0.0, width.get() - leftPadding.get() - rightPadding.get()); },
                        "Control.availableWidth");
    availableHeight.bind([this] { return std::max(0.0, height.get() - topPadding.get() - bottomPadding.get()); },
                         "Control.availableHeight");
    implicitContentWidth.bind([this] { return contentItem.get() ? contentItem.get()->implicitWidth.get() : 0.0; },
                              "Control.implicitContentWidth");
    implicitContentHeight.bind([this] { return contentItem.get() ? contentItem.get()->implicitHeight.get() : 0.0; },
                               "Control.implicitContentHeight");
    implicitBackgroundWidth.bind([this] { return background.get() ? background.get()->implicitWidth.get() : 0.0; },
                                 "Control.implicitBackgroundWidth");
    implicitBackgroundHeight.bind([this] { return background.get() ? background.get()->implicitHeight.get() : 0.0; },
                                  "Control.implicitBackgroundHeight");
    // The styles' implicit size (Basic, Fusion, org.kde.desktop agree for ii's controls).
    implicitWidth.bind(
        [this] {
          return std::max(implicitBackgroundWidth.get() + leftInset.get() + rightInset.get(),
                          implicitContentWidth.get() + leftPadding.get() + rightPadding.get());
        },
        "Control.implicitWidth");
    implicitHeight.bind(
        [this] {
          return std::max(implicitBackgroundHeight.get() + topInset.get() + bottomInset.get(),
                          implicitContentHeight.get() + topPadding.get() + bottomPadding.get());
        },
        "Control.implicitHeight");
    background.changed().connectForever([this] { adopt(background.peek(), true); });
    contentItem.changed().connectForever([this] { adopt(contentItem.peek(), false); });
  }

  void Control::adopt(Item* item, bool isBackground) {
    if (item == nullptr) {
      return;
    }
    item->parent.set(this);
    if (isBackground) {
      item->z.set(-1.0);
      // QQuickControlPrivate::resizeBackground: fill the control within the insets unless the
      // background was sized or anchored itself.
      if (item->followsImplicitSize(Axis::Horizontal)) {
        item->x.bind([this] { return leftInset.get(); }, "Control.background.x");
        item->width.bind([this] { return width.get() - leftInset.get() - rightInset.get(); }, "Control.background.width");
      }
      if (item->followsImplicitSize(Axis::Vertical)) {
        item->y.bind([this] { return topInset.get(); }, "Control.background.y");
        item->height.bind([this] { return height.get() - topInset.get() - bottomInset.get(); },
                          "Control.background.height");
      }
    } else {
      // QQuickControlPrivate::resizeContent: at the paddings, of the available size.
      if (item->anchorsIfAny() == nullptr) {
        item->x.bind([this] { return leftPadding.get(); }, "Control.contentItem.x");
        item->y.bind([this] { return topPadding.get(); }, "Control.contentItem.y");
        item->width.bind([this] { return availableWidth.get(); }, "Control.contentItem.width");
        item->height.bind([this] { return availableHeight.get(); }, "Control.contentItem.height");
      }
    }
  }

  ProgressBar::ProgressBar() {
    position.bind(
        [this] {
          const double f = from.get();
          const double t = to.get();
          return std::abs(f - t) < 1e-12 ? 0.0 : (value.get() - f) / (t - f);
        },
        "ProgressBar.position");
    visualPosition.bind([this] { return mirrored.get() ? 1.0 - position.get() : position.get(); },
                        "ProgressBar.visualPosition");
  }

  void ProgressBar::componentComplete() {
    // QQuickProgressBar::setValue bounds the value once the component is complete.
    auto clamp = [this] {
      const double f = from.peek();
      const double t = to.peek();
      const double bounded = f > t ? std::clamp(value.peek(), t, f) : std::clamp(value.peek(), f, t);
      if (bounded != value.peek()) {
        value.writeDirect(bounded);
      }
    };
    value.changed().connectForever(clamp);
    from.changed().connectForever(clamp);
    to.changed().connectForever(clamp);
    clamp();
  }

} // namespace ii
