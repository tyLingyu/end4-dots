#include "runtime/canvas.h"

#include "render/scene/image_node.h"

#include <cairo.h>

#include <cmath>

namespace ii {

  namespace {
    bool finite(std::initializer_list<double> values) {
      for (const double v : values) {
        if (!std::isfinite(v)) {
          return false;
        }
      }
      return true;
    }

    CanvasTextureSink* g_textureSink = nullptr;
  } // namespace

  void setCanvasTextureSink(CanvasTextureSink* sink) { g_textureSink = sink; }

  // ── Context2D ──────────────────────────────────────────────────────────────

  Context2D::Context2D() = default;

  Context2D::~Context2D() {
    if (m_cr != nullptr) {
      cairo_destroy(m_cr);
    }
  }

  void Context2D::attach(cairo_surface_t* surface, double devicePixelRatio) {
    if (m_cr != nullptr) {
      cairo_destroy(m_cr);
    }
    m_cr = cairo_create(surface);
    cairo_scale(m_cr, devicePixelRatio, devicePixelRatio);
    cairo_set_line_width(m_cr, m_lineWidth);
  }

  void Context2D::setSource(const Color& color) {
    cairo_set_source_rgba(m_cr, color.r, color.g, color.b, color.a * m_state.globalAlpha);
  }

  void Context2D::clearRect(double x, double y, double w, double h) {
    if (m_cr == nullptr || !finite({x, y, w, h})) {
      return;
    }
    cairo_save(m_cr);
    cairo_new_path(m_cr);
    cairo_rectangle(m_cr, x, y, w, h);
    cairo_set_operator(m_cr, CAIRO_OPERATOR_CLEAR);
    cairo_fill(m_cr);
    cairo_restore(m_cr);
  }

  void Context2D::fillRect(double x, double y, double w, double h) {
    if (m_cr == nullptr || !finite({x, y, w, h})) {
      return;
    }
    cairo_save(m_cr);
    cairo_new_path(m_cr);
    cairo_rectangle(m_cr, x, y, w, h);
    setSource(m_state.fill);
    cairo_fill(m_cr);
    cairo_restore(m_cr);
  }

  void Context2D::setFillStyle(const Color& color) { m_state.fill = color; }
  void Context2D::setStrokeStyle(const Color& color) { m_state.stroke = color; }

  void Context2D::setLineWidth(double width) {
    if (!std::isfinite(width) || width <= 0.0) {
      return;  // ignored, as the spec and Qt do
    }
    m_lineWidth = width;
    if (m_cr != nullptr) {
      cairo_set_line_width(m_cr, width);
    }
  }

  void Context2D::setLineCap(std::string_view cap) {
    if (m_cr == nullptr) {
      return;
    }
    if (cap == "butt") {
      cairo_set_line_cap(m_cr, CAIRO_LINE_CAP_BUTT);
    } else if (cap == "round") {
      cairo_set_line_cap(m_cr, CAIRO_LINE_CAP_ROUND);
    } else if (cap == "square") {
      cairo_set_line_cap(m_cr, CAIRO_LINE_CAP_SQUARE);
    }
  }

  void Context2D::setLineJoin(std::string_view join) {
    if (m_cr == nullptr) {
      return;
    }
    if (join == "miter") {
      cairo_set_line_join(m_cr, CAIRO_LINE_JOIN_MITER);
    } else if (join == "round") {
      cairo_set_line_join(m_cr, CAIRO_LINE_JOIN_ROUND);
    } else if (join == "bevel") {
      cairo_set_line_join(m_cr, CAIRO_LINE_JOIN_BEVEL);
    }
  }

  void Context2D::setGlobalAlpha(double alpha) {
    if (std::isfinite(alpha) && alpha >= 0.0 && alpha <= 1.0) {
      m_state.globalAlpha = alpha;
    }
  }

  void Context2D::beginPath() {
    if (m_cr != nullptr) {
      cairo_new_path(m_cr);
    }
  }

  void Context2D::closePath() {
    if (m_cr != nullptr) {
      cairo_close_path(m_cr);
    }
  }

  void Context2D::moveTo(double x, double y) {
    if (m_cr != nullptr && finite({x, y})) {
      cairo_move_to(m_cr, x, y);
    }
  }

  void Context2D::lineTo(double x, double y) {
    // On an empty path Cairo's line_to starts a subpath, as the HTML spec requires.
    if (m_cr != nullptr && finite({x, y})) {
      cairo_line_to(m_cr, x, y);
    }
  }

  void Context2D::arc(double x, double y, double radius, double startAngle, double endAngle, bool anticlockwise) {
    if (m_cr == nullptr || !finite({x, y, radius, startAngle, endAngle}) || radius < 0.0) {
      return;
    }
    (anticlockwise ? cairo_arc_negative : cairo_arc)(m_cr, x, y, radius, startAngle, endAngle);
  }

  void Context2D::rect(double x, double y, double w, double h) {
    if (m_cr != nullptr && finite({x, y, w, h})) {
      cairo_rectangle(m_cr, x, y, w, h);
    }
  }

  void Context2D::stroke() {
    if (m_cr != nullptr) {
      setSource(m_state.stroke);
      cairo_stroke_preserve(m_cr);  // the path stays current, as on a canvas
    }
  }

  void Context2D::fill() {
    if (m_cr != nullptr) {
      setSource(m_state.fill);
      cairo_fill_preserve(m_cr);
    }
  }

  void Context2D::save() {
    if (m_cr != nullptr) {
      cairo_save(m_cr);
      m_saved.push_back(m_state);
    }
  }

  void Context2D::restore() {
    if (m_cr != nullptr && !m_saved.empty()) {
      cairo_restore(m_cr);
      m_state = m_saved.back();
      m_saved.pop_back();
      m_lineWidth = cairo_get_line_width(m_cr);
    }
  }

  void Context2D::translate(double x, double y) {
    if (m_cr != nullptr && finite({x, y})) {
      cairo_translate(m_cr, x, y);
    }
  }

  void Context2D::scale(double x, double y) {
    if (m_cr != nullptr && finite({x, y})) {
      cairo_scale(m_cr, x, y);
    }
  }

  void Context2D::rotate(double angle) {
    if (m_cr != nullptr && std::isfinite(angle)) {
      cairo_rotate(m_cr, angle);
    }
  }

  // ── Canvas ─────────────────────────────────────────────────────────────────

  Canvas::Canvas() : Item(std::make_unique<ImageNode>()) {}

  Canvas::~Canvas() {
    if (g_textureSink != nullptr) {
      g_textureSink->canvasDestroyed(*this);
    }
    if (m_surface != nullptr) {
      cairo_surface_destroy(m_surface);
    }
  }

  Context2D* Canvas::getContext(std::string_view type) {
    if (type != "2d") {
      return nullptr;
    }
    ensureSurface();
    return &m_context;
  }

  void Canvas::requestPaint() {
    m_paintRequested = true;
    polishLater();
  }

  void Canvas::setDevicePixelRatio(double ratio) {
    if (ratio > 0.0 && ratio != m_devicePixelRatio) {
      m_devicePixelRatio = ratio;
      if (m_surface != nullptr) {
        cairo_surface_destroy(m_surface);
        m_surface = nullptr;
      }
      requestPaint();
    }
  }

  void Canvas::geometryChanged() {
    // A new size means a new surface; Qt repaints a resized canvas.
    if (m_surface != nullptr) {
      cairo_surface_destroy(m_surface);
      m_surface = nullptr;
    }
    requestPaint();
  }

  void Canvas::ensureSurface() {
    if (m_surface != nullptr) {
      return;
    }
    const int w = std::max(1, static_cast<int>(std::ceil(width.peek() * m_devicePixelRatio)));
    const int h = std::max(1, static_cast<int>(std::ceil(height.peek() * m_devicePixelRatio)));
    m_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    m_context.attach(m_surface, m_devicePixelRatio);
  }

  void Canvas::polish() {
    if (!m_paintRequested || width.peek() <= 0.0 || height.peek() <= 0.0) {
      return;
    }
    m_paintRequested = false;
    ensureSurface();
    paint.emit(Rect{0.0, 0.0, width.peek(), height.peek()});
    cairo_surface_flush(m_surface);
    m_pixelsDirty = true;
    if (g_textureSink != nullptr) {
      g_textureSink->canvasPainted(*this);
    }
    painted.emit();
  }

} // namespace ii
