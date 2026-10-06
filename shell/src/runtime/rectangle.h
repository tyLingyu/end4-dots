#pragma once

#include "runtime/color.h"
#include "runtime/item.h"

class RectNode;

namespace ii {

  // QQuickRectangle: filled rounded rectangle with an inner border.
  class Rectangle : public Item {
  public:
    // Owned objects (children) go first, while this object's own members are intact: their
    // teardown notifies, and this object's bindings may still run.
    ~Rectangle() override { destroyOwned(); }
    Rectangle();

    Property<Color> color{Color{1.0F, 1.0F, 1.0F, 1.0F}}; // QML default: white
    Property<double> radius;

    struct Border {
      Property<double> width;
      Property<Color> color{Color{0.0F, 0.0F, 0.0F, 1.0F}};
    } border;

  protected:
    void geometryChanged() override;

  private:
    static void syncStyle(void* self);
    [[nodiscard]] RectNode& rectNode() const noexcept;
  };

} // namespace ii
