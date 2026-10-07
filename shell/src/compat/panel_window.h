#pragma once

// Quickshell PanelWindow (with the WlrLayershell attached properties) and Region.

#include "compat/screen.h"
#include "runtime/color.h"
#include "runtime/geometry.h"
#include "runtime/item.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <cstdint>
#include <memory>
#include <string>

class InputDispatcher;
class LayerSurface;
struct PointerEvent;

namespace ii {
  class Rectangle;
}

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

  // A wlr-layer-shell surface showing contentItem (Quickshell's WlrLayershell over ProxyWindowBase).
  // It is mapped while visible, completed and the platform is set, on `screen`'s output (or one
  // the compositor picks while screen is null), and unmapped otherwise: Quickshell never reuses an
  // invisible layer window either. Anchors, margins, layer, exclusive zone, size and keyboard focus
  // are committed together before the next frame, as Quickshell's polish does.
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
    // The window's size: the implicit size until the compositor configures another.
    Property<int> width;
    Property<int> height;
    Property<ShellScreen*> screen;
    Property<Region*> mask;
    Property<double> devicePixelRatio{1.0};

    // Visual children go here (Quickshell's contentItem); it fills the window.
    [[nodiscard]] Item* contentItem() const noexcept { return m_contentItem; }

    // The exclusive zone sent to the compositor (WlrLayershell's bcExclusiveZone).
    [[nodiscard]] int effectiveExclusiveZone() const;
    [[nodiscard]] bool isMapped() const noexcept { return m_surface != nullptr; }

  protected:
    void componentComplete() override;

  private:
    void updateMapped();
    void map();
    void unmap();
    void scheduleCommit();
    void commitState();
    void applyMask();
    void onPointer(const PointerEvent& event);
    void prepareFrame();
    void frameTick();
    [[nodiscard]] std::uint32_t anchorBits() const;

    Item* m_contentItem = nullptr;
    Rectangle* m_background = nullptr;
    std::unique_ptr<LayerSurface> m_surface;
    std::unique_ptr<InputDispatcher> m_input;
    Connection m_screenDestroyed;
    Connection m_platformChanged;
    // The mask item's rectangle in window coordinates, tracking the item and its ancestors.
    Property<Rect> m_maskRect;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::uint32_t m_mappedAnchors = 0;
    std::string m_mappedNamespace;
    bool m_commitQueued = false;
  };

} // namespace ii::qs
