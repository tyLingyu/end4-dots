#pragma once

// Quickshell PanelWindow (with the WlrLayershell attached properties) and Region.

#include "compat/screen.h"
#include "runtime/color.h"
#include "runtime/item.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <memory>
#include <string>

namespace ii::qs {

  struct WlrLayer {
    enum Enum { Background = 0, Bottom = 1, Top = 2, Overlay = 3 };
  };
  struct WlrKeyboardFocus {
    enum Enum { None = 0, Exclusive = 1, OnDemand = 2 };
  };
  struct ExclusionMode {
    enum Enum { Normal = 0, Ignore = 1, Auto = 2 };
  };

  // An input or mask region (PendingRegion): an item's rectangle, or an explicit one.
  class Region : public Object {
  public:
    Property<Item*> item;
    Property<int> shape;
    Property<int> intersection;
    Property<int> x;
    Property<int> y;
    Property<int> width;
    Property<int> height;
  };

  class PanelWindow : public Object {
  public:
    struct Anchors {
      Property<bool> left;
      Property<bool> right;
      Property<bool> top;
      Property<bool> bottom;
    };
    struct Margins {
      Property<int> left;
      Property<int> right;
      Property<int> top;
      Property<int> bottom;
    };
    struct Layershell {
      Property<WlrLayer::Enum> layer{WlrLayer::Top};
      Property<std::string> namespace_{"quickshell"};
      Property<WlrKeyboardFocus::Enum> keyboardFocus{WlrKeyboardFocus::None};
    };

    PanelWindow();
    ~PanelWindow() override;

    Anchors anchors;
    Margins margins;
    Layershell layershell;
    Property<int> exclusiveZone;
    Property<ExclusionMode::Enum> exclusionMode{ExclusionMode::Auto};
    Property<bool> aboveWindows{true};
    Property<bool> focusable;
    Property<Color> color{Color{1.0F, 1.0F, 1.0F, 1.0F}};
    Property<bool> visible{true};
    Property<int> implicitWidth;
    Property<int> implicitHeight;
    Property<int> width;
    Property<int> height;
    Property<ShellScreen*> screen;
    Property<Region*> mask;
    Property<double> devicePixelRatio{1.0};

    // Visual children go here (Quickshell's contentItem).
    [[nodiscard]] Item* contentItem() const noexcept { return m_contentItem; }

  private:
    Item* m_contentItem = nullptr;
  };

} // namespace ii::qs
