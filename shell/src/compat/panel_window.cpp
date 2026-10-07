#include "compat/panel_window.h"

#include "compat/canvas_textures.h"
#include "compat/platform.h"
#include "core/deferred_call.h"
#include "core/log.h"
#include "render/scene/input_dispatcher.h"
#include "runtime/animation.h"
#include "runtime/canvas.h"
#include "runtime/rectangle.h"
#include "wayland/layer_surface.h"
#include "wayland/wayland_connection.h"
#include "wayland/wayland_seat.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>
#include <wayland-client.h>

namespace ii::qs {

  namespace {

    constexpr Logger kLog("panelwindow");

    // The surface size Quickshell requests (wlr_layershell/surface.cpp, constrainedSize): 0 on an
    // axis anchored at both ends (the compositor stretches it), else at least 1.
    std::uint32_t constrained(bool stretched, int size) {
      return stretched ? 0U : static_cast<std::uint32_t>(std::max(1, size));
    }

    void setCanvasRatio(Item* item, double ratio) {
      if (auto* canvas = dynamic_cast<Canvas*>(item)) {
        canvas->setDevicePixelRatio(ratio);
      }
      for (Item* child : item->childItems()) {
        setCanvasRatio(child, ratio);
      }
    }

  } // namespace

  PanelWindow::PanelWindow() {
    m_contentItem = create<Item>();
    m_contentItem->width.bind([this] { return static_cast<double>(width.get()); });
    m_contentItem->height.bind([this] { return static_cast<double>(height.get()); });
    // The window's clear colour, behind every child.
    m_background = m_contentItem->add<Rectangle>();
    m_background->anchors().fill.set(m_contentItem);
    m_background->color.bind([this] { return color.get(); });
    m_background->visible.bind([this] { return color.get().a > 0.0F; });

    // Until configured, the window is the size it asks for (ProxyWindowBase's implicit size).
    width.bind([this] { return implicitWidth.get(); });
    height.bind([this] { return implicitHeight.get(); });

    // WlrLayershell: aboveWindows and focusable are views of layer and keyboardFocus.
    aboveWindows.onChanged([this] {
      layershell.layer.set(aboveWindows.peek() ? WlrLayer::Top : WlrLayer::Bottom);
    });
    focusable.onChanged([this] {
      layershell.keyboardFocus.set(focusable.peek() ? WlrKeyboardFocus::OnDemand : WlrKeyboardFocus::None);
    });

    for (PropertyBase* property : std::initializer_list<PropertyBase*>{
             &anchors.left, &anchors.right, &anchors.top, &anchors.bottom, &margins.left, &margins.right,
             &margins.top, &margins.bottom, &layershell.layer, &layershell.keyboardFocus, &exclusiveZone,
             &exclusionMode, &implicitWidth, &implicitHeight, &layershell.namespace_
         }) {
      property->changed().connectForever([this] { scheduleCommit(); });
    }
    visible.onChanged([this] { updateMapped(); });
    screen.onChanged([this] {
      m_screenDestroyed.disconnect();
      if (ShellScreen* s = screen.peek()) {
        // A screen whose output went away is destroyed; Quickshell's window then has none.
        m_screenDestroyed = s->destroyed.connect([this] { screen.set(nullptr); });
      }
      if (isMapped()) {
        unmap();
        updateMapped();
      }
    });

    m_maskRect.bind([this] {
      Region* region = mask.get();
      if (region == nullptr) {
        return Rect{};
      }
      Item* item = region->item.get();
      if (item == nullptr) {
        return Rect{static_cast<double>(region->x.get()), static_cast<double>(region->y.get()),
                    static_cast<double>(region->width.get()), static_cast<double>(region->height.get())};
      }
      Rect rect{0.0, 0.0, item->width.get(), item->height.get()};
      for (Item* at = item; at != nullptr && at != m_contentItem; at = at->parent.get()) {
        rect.x += at->x.get();
        rect.y += at->y.get();
      }
      return rect;
    });
    m_maskRect.onChanged([this] { applyMask(); });
    mask.onChanged([this] { applyMask(); });

    m_platformChanged = platformChanged().connect([this] {
      unmap();
      updateMapped();
    });
  }

  PanelWindow::~PanelWindow() {
    *m_alive = false;
    m_alive.reset();
    unmap();
    destroyOwned();
  }

  void PanelWindow::componentComplete() { updateMapped(); }

  int PanelWindow::effectiveExclusiveZone() const {
    switch (exclusionMode.peek()) {
    case ExclusionMode::Ignore: return -1;
    case ExclusionMode::Normal: return exclusiveZone.peek();
    case ExclusionMode::Auto: {
      // The edge the panel is attached to: exactly one of left/right, or of top/bottom.
      const bool hEdge = anchors.left.peek() != anchors.right.peek();
      const bool vEdge = anchors.top.peek() != anchors.bottom.peek();
      if (vEdge && !hEdge) {
        return implicitHeight.peek() + (anchors.top.peek() ? margins.bottom.peek() : margins.top.peek());
      }
      if (hEdge && !vEdge) {
        return implicitWidth.peek() + (anchors.left.peek() ? margins.right.peek() : margins.left.peek());
      }
      return 0;
    }
    }
    return 0;
  }

  std::uint32_t PanelWindow::anchorBits() const {
    return (anchors.top.peek() ? LayerShellAnchor::Top : 0U) | (anchors.bottom.peek() ? LayerShellAnchor::Bottom : 0U)
        | (anchors.left.peek() ? LayerShellAnchor::Left : 0U) | (anchors.right.peek() ? LayerShellAnchor::Right : 0U);
  }

  void PanelWindow::updateMapped() {
    const bool wanted = visible.peek() && isCompleted() && waylandConnection() != nullptr && renderContext() != nullptr;
    if (wanted && !isMapped()) {
      map();
    } else if (!wanted && isMapped()) {
      unmap();
    }
  }

  void PanelWindow::map() {
    WaylandConnection& wayland = *waylandConnection();
    const std::uint32_t anchor = anchorBits();
    const bool stretchW = anchors.left.peek() && anchors.right.peek();
    const bool stretchH = anchors.top.peek() && anchors.bottom.peek();
    const std::uint32_t w = constrained(stretchW, implicitWidth.peek());
    const std::uint32_t h = constrained(stretchH, implicitHeight.peek());
    m_surface = std::make_unique<LayerSurface>(
        wayland,
        LayerSurfaceConfig{
            .nameSpace = layershell.namespace_.peek(),
            .layer = static_cast<LayerShellLayer>(layershell.layer.peek()),
            .anchor = anchor,
            .width = w,
            .height = h,
            .exclusiveZone = effectiveExclusiveZone(),
            .marginTop = margins.top.peek(),
            .marginRight = margins.right.peek(),
            .marginBottom = margins.bottom.peek(),
            .marginLeft = margins.left.peek(),
            .keyboard = static_cast<LayerShellKeyboard>(layershell.keyboardFocus.peek()),
            .defaultWidth = std::max(1U, w),
            .defaultHeight = std::max(1U, h),
        }
    );
    m_mappedAnchors = anchor;
    m_mappedNamespace = layershell.namespace_.peek();
    m_surface->setRenderContext(renderContext());
    m_surface->setConfigureCallback([this](std::uint32_t cw, std::uint32_t ch) {
      width.writeDirect(static_cast<int>(cw));
      height.writeDirect(static_cast<int>(ch));
      m_surface->requestUpdate();
    });
    m_surface->setScaleChangedCallback([this](float scale) {
      devicePixelRatio.set(static_cast<double>(scale));
      setCanvasRatio(m_contentItem, static_cast<double>(scale));
    });
    m_surface->setPrepareFrameCallback([this](bool, bool) { prepareFrame(); });
    m_surface->setFrameTickCallback([this](float) { frameTick(); });
    m_surface->setClosedCallback([this] {
      // The compositor closed the layer (its output went away): drop it, as Quickshell closes it.
      DeferredCall::callLater([this, alive = std::weak_ptr<bool>(m_alive)] {
        if (!alive.expired()) {
          unmap();
        }
      });
    });
    m_surface->setSceneRoot(m_contentItem->node());

    m_input = std::make_unique<InputDispatcher>();
    m_input->setSceneRoot(m_contentItem->node());
    m_input->setCursorShapeCallback([&wayland](std::uint32_t serial, std::uint32_t shape) {
      wayland.setCursorShape(serial, shape);
    });

    wl_output* output = screen.peek() != nullptr ? outputForScreen(screen.peek()) : nullptr;
    if (screen.peek() != nullptr && output == nullptr) {
      kLog.warn("screen {} has no output; letting the compositor pick", screen.peek()->name.peek());
    }
    if (!m_surface->initialize(output)) {
      kLog.warn("failed to create layer surface {}", m_mappedNamespace);
      m_input.reset();
      m_surface.reset();
      return;
    }
    devicePixelRatio.set(static_cast<double>(m_surface->effectiveBufferScale()));
    setCanvasRatio(m_contentItem, devicePixelRatio.peek());
    applyMask();

    LayerSurface* surface = m_surface.get();
    registerWindow(
        surface->wlSurface(),
        PlatformWindow{
            .pointerEvent = [this](const PointerEvent& event) { onPointer(event); },
            .requestUpdate = [surface] { surface->requestUpdate(); },
            .requestFrameTick = [surface] { surface->requestFrameTick(); },
        }
    );
    if (hasPendingPolish()) {
      surface->requestUpdate();
    }
    if (AnimationDriver::instance().hasRunningAnimations()) {
      surface->requestFrameTick();
    }
  }

  void PanelWindow::unmap() {
    if (!m_surface) {
      return;
    }
    unregisterWindow(m_surface->wlSurface());
    // This may run from one of the surface's own callbacks: silence it now, destroy it later.
    m_surface->setConfigureCallback({});
    m_surface->setScaleChangedCallback({});
    m_surface->setPrepareFrameCallback({});
    m_surface->setFrameTickCallback({});
    m_surface->setClosedCallback({});
    m_surface->setSceneRoot(nullptr);
    std::shared_ptr<LayerSurface> surface(std::move(m_surface));
    std::shared_ptr<InputDispatcher> input(std::move(m_input));
    DeferredCall::callLater([surface, input] {});
    m_commitQueued = false;
  }

  void PanelWindow::scheduleCommit() {
    if (!isMapped() || m_commitQueued) {
      return;
    }
    m_commitQueued = true;
    DeferredCall::callLater([this, alive = std::weak_ptr<bool>(m_alive)] {
      if (!alive.expired()) {
        commitState();
      }
    });
  }

  // WlrLayershell::onPolished -> LayerSurface::commit: send what changed since the last commit.
  void PanelWindow::commitState() {
    m_commitQueued = false;
    if (!isMapped()) {
      return;
    }
    // The namespace and anchors are fixed when the surface is created here (Noctalia's surface
    // has no re-anchoring); a change recreates the window.
    if (anchorBits() != m_mappedAnchors || layershell.namespace_.peek() != m_mappedNamespace) {
      unmap();
      updateMapped();
      return;
    }
    m_surface->setLayer(static_cast<LayerShellLayer>(layershell.layer.peek()));
    m_surface->setMargins(margins.top.peek(), margins.right.peek(), margins.bottom.peek(), margins.left.peek());
    m_surface->setExclusiveZone(effectiveExclusiveZone());
    m_surface->setKeyboardInteractivity(static_cast<LayerShellKeyboard>(layershell.keyboardFocus.peek()));
    const bool stretchW = anchors.left.peek() && anchors.right.peek();
    const bool stretchH = anchors.top.peek() && anchors.bottom.peek();
    const std::uint32_t w = constrained(stretchW, implicitWidth.peek());
    const std::uint32_t h = constrained(stretchH, implicitHeight.peek());
    // requestSize reads 0 as "keep the current size" unless the axis is stretched.
    m_surface->requestSize(w, h);
  }

  void PanelWindow::applyMask() {
    if (!isMapped()) {
      return;
    }
    if (mask.peek() == nullptr) {
      wl_surface_set_input_region(m_surface->wlSurface(), nullptr);
    } else {
      const Rect r = m_maskRect.peek();
      m_surface->setInputRegion({InputRect{
          .x = static_cast<int>(std::floor(r.x)),
          .y = static_cast<int>(std::floor(r.y)),
          .width = static_cast<int>(std::ceil(r.width)),
          .height = static_cast<int>(std::ceil(r.height)),
      }});
    }
    m_surface->requestUpdate();
  }

  void PanelWindow::onPointer(const PointerEvent& event) {
    if (!m_input) {
      return;
    }
    const auto x = static_cast<float>(event.sx);
    const auto y = static_cast<float>(event.sy);
    switch (event.type) {
    case PointerEvent::Type::Enter: m_input->pointerEnter(x, y, event.serial); break;
    case PointerEvent::Type::Leave: m_input->pointerLeave(); break;
    case PointerEvent::Type::Motion: m_input->pointerMotion(x, y, event.serial); break;
    case PointerEvent::Type::Button:
      m_input->pointerButton(x, y, event.button, event.pressed, event.serial, event.time, event.touch);
      break;
    case PointerEvent::Type::Axis:
      m_input->pointerAxis(
          x, y, event.axis, event.axisSource, event.axisValue, event.axisDiscrete, event.axisValue120, event.axisLines,
          event.axisGestureSerial
      );
      break;
    }
  }

  // Before laying out and drawing: deferred layout work, then the pixels canvases painted.
  void PanelWindow::prepareFrame() {
    flushPolish();
    uploadCanvasTextures();
    if (AnimationDriver::instance().hasRunningAnimations() && m_surface) {
      m_surface->requestFrameTick();
    }
  }

  void PanelWindow::frameTick() {
    auto& driver = AnimationDriver::instance();
    driver.tick();
    flushPolish();
    uploadCanvasTextures();
    if (driver.hasRunningAnimations() && m_surface) {
      m_surface->requestFrameTick();
    }
  }

} // namespace ii::qs
