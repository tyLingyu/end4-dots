#pragma once

#include "runtime/color.h"
#include "runtime/geometry.h"
#include "runtime/item.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;

namespace ii {

  // The "2d" context of a QML Canvas: the HTML Canvas 2D API over Cairo, with QQuickContext2D's
  // rules (invalid values for lineWidth/lineCap are ignored, non-finite coordinates are ignored,
  // lineTo on an empty path starts one). State persists between paints, as in Qt.
  class Context2D {
  public:
    Context2D();
    ~Context2D();
    Context2D(const Context2D&) = delete;
    Context2D& operator=(const Context2D&) = delete;

    void clearRect(double x, double y, double w, double h);
    void fillRect(double x, double y, double w, double h);
    void setFillStyle(const Color& color);
    void setStrokeStyle(const Color& color);
    void setLineWidth(double width);
    [[nodiscard]] double lineWidth() const noexcept { return m_lineWidth; }
    void setLineCap(std::string_view cap);
    void setLineJoin(std::string_view join);
    void setGlobalAlpha(double alpha);

    void beginPath();
    void closePath();
    void moveTo(double x, double y);
    void lineTo(double x, double y);
    void arc(double x, double y, double radius, double startAngle, double endAngle, bool anticlockwise = false);
    void rect(double x, double y, double w, double h);
    void stroke();
    void fill();

    void save();
    void restore();
    void translate(double x, double y);
    void scale(double x, double y);
    void rotate(double angle);

    // The canvas sets the surface it draws on (resized canvases get a new one).
    void attach(cairo_surface_t* surface, double devicePixelRatio);

  private:
    struct State {
      Color fill{0.0F, 0.0F, 0.0F, 1.0F};
      Color stroke{0.0F, 0.0F, 0.0F, 1.0F};
      double globalAlpha = 1.0;
    };

    void setSource(const Color& color);

    cairo_t* m_cr = nullptr;
    double m_lineWidth = 1.0;
    State m_state;
    std::vector<State> m_saved;
  };

  // QML Canvas: requestPaint() schedules onPaint before the next frame; what it draws is kept as
  // device-pixel BGRA (premultiplied) for the renderer to upload.
  class Canvas : public Item {
  public:
    Canvas();
    ~Canvas() override;

    Property<int> renderTarget;
    Property<int> renderStrategy;
    Property<std::string> contextType;
    Property<bool> available{true};
    Signal<const Rect&> paint;
    Signal<> painted;

    // getContext("2d"), or nullptr for another type.
    [[nodiscard]] Context2D* getContext(std::string_view type);
    void requestPaint();

    // Device pixels per logical pixel of the window showing the canvas.
    void setDevicePixelRatio(double ratio);

    // What the last paint produced: the renderer uploads it when dirty and clears the flag.
    [[nodiscard]] cairo_surface_t* surface() const noexcept { return m_surface; }
    [[nodiscard]] bool pixelsDirty() const noexcept { return m_pixelsDirty; }
    void markPixelsUploaded() noexcept { m_pixelsDirty = false; }

    void polish() override;

  protected:
    void geometryChanged() override;

  private:
    void ensureSurface();

    Context2D m_context;
    cairo_surface_t* m_surface = nullptr;
    double m_devicePixelRatio = 1.0;
    bool m_paintRequested = false;
    bool m_pixelsDirty = false;
  };

} // namespace ii
