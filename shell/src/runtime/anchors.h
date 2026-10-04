#pragma once

#include "runtime/property.h"

#include <cstdint>

namespace ii {

  class Item;

  enum class AnchorEdge : std::uint8_t { None, Left, Right, HorizontalCenter, Top, Bottom, VerticalCenter };

  // `someItem.left` etc.: an edge of an item, the value an anchor binds to.
  struct AnchorLine {
    Item* item = nullptr;
    AnchorEdge edge = AnchorEdge::None;

    [[nodiscard]] bool valid() const noexcept { return item != nullptr && edge != AnchorEdge::None; }
    friend bool operator==(const AnchorLine&, const AnchorLine&) = default;
  };

  // `anchors.*`. Anchoring is implemented by installing bindings on the item's x/y/width/height,
  // so anchored geometry follows its targets through the ordinary binding machinery.
  // Rounding follows QQuickAnchors (calibrated by tests/diff): with alignWhenCentered, an item's
  // half-extent is qRound(extent / 2), on both the anchored item and the item it centers on.
  class Anchors {
  public:
    explicit Anchors(Item& item);
    ~Anchors();

    Anchors(const Anchors&) = delete;
    Anchors& operator=(const Anchors&) = delete;

    Property<Item*> fill;
    Property<Item*> centerIn;
    Property<AnchorLine> left;
    Property<AnchorLine> right;
    Property<AnchorLine> horizontalCenter;
    Property<AnchorLine> top;
    Property<AnchorLine> bottom;
    Property<AnchorLine> verticalCenter;

    Property<double> margins;
    Property<double> leftMargin;
    Property<double> rightMargin;
    Property<double> topMargin;
    Property<double> bottomMargin;
    Property<double> horizontalCenterOffset;
    Property<double> verticalCenterOffset;
    Property<bool> alignWhenCentered{true};

    // Half of the item's width/height as anchoring sees it (QQuickAnchors' hcenter/vcenter).
    [[nodiscard]] static double halfWidth(const Item& item);
    [[nodiscard]] static double halfHeight(const Item& item);

  private:
    static void onHorizontalChanged(void* self);
    static void onVerticalChanged(void* self);
    void updateHorizontal();
    void updateVertical();

    Item& m_item;
    // The bindings this object installed, so it never removes one the user installed later.
    const Binding* m_xBinding = nullptr;
    const Binding* m_widthBinding = nullptr;
    const Binding* m_yBinding = nullptr;
    const Binding* m_heightBinding = nullptr;
  };

} // namespace ii
