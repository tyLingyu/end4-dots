#pragma once

#include "runtime/item.h"

#include <array>
#include <limits>
#include <optional>

namespace ii {

  // Qt::Alignment values, so translated `Qt.AlignRight` etc. map one to one.
  namespace Align {
    inline constexpr int Left = 0x1;
    inline constexpr int Right = 0x2;
    inline constexpr int HCenter = 0x4;
    inline constexpr int Top = 0x20;
    inline constexpr int Bottom = 0x40;
    inline constexpr int VCenter = 0x80;
    inline constexpr int Center = HCenter | VCenter;
    inline constexpr int HorizontalMask = Left | Right | HCenter;
    inline constexpr int VerticalMask = Top | Bottom | VCenter;
  } // namespace Align

  // `Layout.*` attached properties of an item inside a RowLayout/ColumnLayout.
  class LayoutAttached {
  public:
    LayoutAttached();

    // Unset means "the default": false for ordinary items, "does my content fill?" for layouts.
    Property<std::optional<bool>> fillWidth;
    Property<std::optional<bool>> fillHeight;
    Property<double> minimumWidth;
    Property<double> minimumHeight;
    // Negative means "use the implicit size".
    Property<double> preferredWidth{-1.0};
    Property<double> preferredHeight{-1.0};
    Property<double> maximumWidth{std::numeric_limits<double>::infinity()};
    Property<double> maximumHeight{std::numeric_limits<double>::infinity()};
    Property<int> alignment;
    Property<double> margins;
    Property<double> leftMargin;
    Property<double> rightMargin;
    Property<double> topMargin;
    Property<double> bottomMargin;

  private:
    friend class LinearLayout;
    // An explicit width/height the item had before any layout sized it. Qt uses it as the
    // preferred size when the implicit size is 0 (calibrated: tests/diff layout_explicit_size).
    std::array<std::optional<double>, 2> m_explicitSize;
    std::array<bool, 2> m_sizedByLayout{false, false};
  };

  // QQuickLinearLayout (RowLayout / ColumnLayout). Space distribution follows QGridLayoutEngine
  // as calibrated in tests/diff/cases/layout_*.qml:
  //  - an item that doesn't fill its axis is fixed at its preferred size;
  //  - below the preferred total, items shrink towards their minimum with Qt's
  //    growthFactorBelowPreferredSize weighting; below the minimum total they overflow;
  //  - between preferred and maximum, extra space goes to items that can grow, weighted by
  //    their preferred size (1 if that is 0), water-filling around maxima;
  //  - above the maximum total, cells keep growing (weighted by maximum) and items align in them;
  //  - cell edges snap to whole pixels, then the item is aligned in its cell and snapped again.
  class LinearLayout : public Item {
  public:
    Property<double> spacing{5.0};

    void polish() override;

    struct Hints {
      double minimum = 0.0;
      double preferred = 0.0;
      double maximum = 0.0;
      double marginBefore = 0.0;
      double marginAfter = 0.0;
      bool fill = false;
      int alignment = 0;
    };
    // Size hints of the layout as a whole (what a parent layout sees).
    [[nodiscard]] Hints contentHints(Axis axis) const;

  protected:
    explicit LinearLayout(Axis orientation);

  private:
    struct Entry {
      Item* item = nullptr;
      Hints main;
      Hints cross;
    };

    [[nodiscard]] std::vector<Entry> entries() const;
    [[nodiscard]] static Hints hintsFor(Item& child, Axis axis);

    Axis m_orientation;
    Property<int> m_tracker;
    int m_trackerCount = 0;
  };

  class RowLayout : public LinearLayout {
  public:
    RowLayout() : LinearLayout(Axis::Horizontal) {}
  };

  class ColumnLayout : public LinearLayout {
  public:
    ColumnLayout() : LinearLayout(Axis::Vertical) {}
  };

} // namespace ii
