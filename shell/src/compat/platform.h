#pragma once

// What the compat layer shows windows with: the Wayland connection and the renderer, set by
// main.cpp once connected (tests leave them unset: windows stay unmapped, shortcuts pending).
// Also the frame plumbing every window shares (polish, animation ticks, pointer routing) and
// Quickshell.screens, kept in step with the Wayland outputs.

#include "runtime/signal.h"

#include <functional>

class RenderContext;
class WaylandConnection;
struct PointerEvent;
struct wl_output;
struct wl_surface;

namespace ii::qs {

  class ShellScreen;

  void setPlatform(WaylandConnection* wayland, RenderContext* render);
  [[nodiscard]] WaylandConnection* waylandConnection();
  [[nodiscard]] RenderContext* renderContext();
  // Emitted after setPlatform changed either.
  [[nodiscard]] Signal<>& platformChanged();

  // The output a screen of Quickshell.screens stands for (nullptr once it is gone).
  [[nodiscard]] wl_output* outputForScreen(const ShellScreen* screen);

  // A mapped window: it receives the pointer events of its wl_surface, and is asked for frames
  // when polish or animations need one.
  struct PlatformWindow {
    std::function<void(const PointerEvent&)> pointerEvent;
    std::function<void()> requestUpdate;
    std::function<void()> requestFrameTick;
  };
  void registerWindow(wl_surface* surface, PlatformWindow window);
  void unregisterWindow(wl_surface* surface);

} // namespace ii::qs
