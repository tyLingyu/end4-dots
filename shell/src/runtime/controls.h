#pragma once

#include "runtime/item.h"

namespace ii {

  // QtQuick.Templates Control with the sizing the shipped styles give it: implicit size from
  // the background and content (plus insets/padding), background placed at the insets, content
  // at the paddings. Both are reparented to the control, the background behind.
  class Control : public Item {
  public:
    Control();

    Property<Item*> background;
    Property<Item*> contentItem;
    Property<double> padding;
    Property<double> leftPadding;
    Property<double> rightPadding;
    Property<double> topPadding;
    Property<double> bottomPadding;
    Property<double> leftInset;
    Property<double> rightInset;
    Property<double> topInset;
    Property<double> bottomInset;
    Property<double> availableWidth;   // read-only
    Property<double> availableHeight;  // read-only
    Property<double> implicitContentWidth;     // read-only
    Property<double> implicitContentHeight;    // read-only
    Property<double> implicitBackgroundWidth;  // read-only
    Property<double> implicitBackgroundHeight; // read-only
    Property<bool> hovered;
    Property<bool> hoverEnabled;
    Property<bool> mirrored;

  private:
    void adopt(Item* item, bool isBackground);
  };

  // QtQuick.Templates ProgressBar (QQuickProgressBar): value kept within [from, to] once complete.
  class ProgressBar : public Control {
  public:
    ProgressBar();

    Property<double> from{0.0};
    Property<double> to{1.0};
    Property<double> value{0.0};
    Property<double> position;        // read-only
    Property<double> visualPosition;  // read-only
    Property<bool> indeterminate;

  protected:
    void componentComplete() override;
  };

} // namespace ii
