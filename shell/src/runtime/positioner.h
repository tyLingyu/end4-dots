#pragma once

#include "runtime/item.h"

namespace ii {

  // QQuickBasePositioner (Row / Column): stacks visible, non-empty children with spacing and
  // padding, at their own sizes, and sizes itself to fit (implicitWidth/implicitHeight).
  class Positioner : public Item {
  public:
    // Owned objects (children) go first, while this object's own members are intact: their
    // teardown notifies, and this object's bindings may still run.
    ~Positioner() override { destroyOwned(); }
    Property<double> spacing;
    Property<double> padding;
    Property<double> leftPadding;
    Property<double> rightPadding;
    Property<double> topPadding;
    Property<double> bottomPadding;

    void polish() override;

  protected:
    explicit Positioner(Axis orientation);

  private:
    // Children a positioner places: visible and with a non-zero size (per the Qt docs).
    [[nodiscard]] std::vector<Item*> placed() const;

    Axis m_orientation;
    Property<int> m_tracker;
    int m_trackerCount = 0;
  };

  class Row : public Positioner {
  public:
    Row() : Positioner(Axis::Horizontal) {}
  };

  class Column : public Positioner {
  public:
    Column() : Positioner(Axis::Vertical) {}
  };

} // namespace ii
