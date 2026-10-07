#include "compat/platform.h"

#include "compat/canvas_textures.h"
#include "compat/quickshell.h"
#include "compat/screen.h"
#include "core/deferred_call.h"
#include "core/timer_manager.h"
#include "runtime/animation.h"
#include "runtime/item.h"
#include "wayland/wayland_connection.h"
#include "wayland/wayland_seat.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ii::qs {

  namespace {

    struct State {
      WaylandConnection* wayland = nullptr;
      RenderContext* render = nullptr;
      Signal<> changed;
      std::vector<std::pair<wl_surface*, PlatformWindow>> windows;
      std::unordered_map<const ShellScreen*, wl_output*> screenOutputs;
      // Drives animations while no window is mapped (Qt's QUnifiedTimer does the same without a
      // render loop), so Behaviors on non-visual properties still finish.
      Timer fallbackTick;
      bool flushQueued = false;
    };

    State& state() {
      static State s;
      return s;
    }

    PlatformWindow* windowFor(wl_surface* surface) {
      auto& windows = state().windows;
      auto it = std::ranges::find(windows, surface, &std::pair<wl_surface*, PlatformWindow>::first);
      return it == windows.end() ? nullptr : &it->second;
    }

    void routePointer(const PointerEvent& event) {
      if (PlatformWindow* window = windowFor(event.surface)) {
        // The handler may unmap the window: keep the callback alive while it runs.
        const auto handler = window->pointerEvent;
        if (handler) {
          handler(event);
        }
      }
    }

    // Polish runs in a window's prepare phase; without windows it runs from the loop.
    void onPolishRequested() {
      auto& s = state();
      if (!s.windows.empty()) {
        for (auto& [surface, window] : s.windows) {
          window.requestUpdate();
        }
        return;
      }
      if (!s.flushQueued) {
        s.flushQueued = true;
        DeferredCall::callLater([] {
          state().flushQueued = false;
          flushPolish();
        });
      }
    }

    void onFrameRequested() {
      auto& s = state();
      if (!s.windows.empty()) {
        for (auto& [surface, window] : s.windows) {
          window.requestFrameTick();
        }
        return;
      }
      if (s.fallbackTick.active()) {
        return;
      }
      s.fallbackTick.startRepeating(std::chrono::milliseconds(16), [] {
        auto& driver = AnimationDriver::instance();
        driver.tick();
        flushPolish();
        if (!driver.hasRunningAnimations() || !state().windows.empty()) {
          state().fallbackTick.stop();
          if (driver.hasRunningAnimations()) {
            onFrameRequested();
          }
        }
      });
    }

    // Quickshell.screens: one ShellScreen per output, in the compositor's order, as
    // QGuiApplication::screens() lists them for Quickshell.
    void syncScreens() {
      auto& s = state();
      auto& quickshell = Quickshell::instance();
      std::vector<ShellScreen*> screens;
      std::unordered_map<const ShellScreen*, wl_output*> outputs;
      const auto& previous = quickshell.screens.peek();
      if (s.wayland != nullptr) {
        for (const WaylandOutput& output : s.wayland->outputs()) {
          if (output.output == nullptr || !output.done) {
            continue;
          }
          auto existing = std::ranges::find_if(previous, [&](const ShellScreen* screen) {
            const auto it = s.screenOutputs.find(screen);
            return it != s.screenOutputs.end() && it->second == output.output;
          });
          ShellScreen* screen = existing != previous.end() ? *existing : quickshell.create<ShellScreen>();
          screen->name.set(output.connectorName);
          screen->model.set(output.model);
          screen->serialNumber.set(output.serialNumber);
          screen->x.set(output.logicalX);
          screen->y.set(output.logicalY);
          screen->width.set(output.effectiveLogicalWidth());
          screen->height.set(output.effectiveLogicalHeight());
          screen->devicePixelRatio.set(static_cast<double>(output.configuredScale()));
          screens.push_back(screen);
          outputs.emplace(screen, output.output);
        }
      }
      std::vector<ShellScreen*> removed;
      for (ShellScreen* screen : previous) {
        if (std::ranges::find(screens, screen) == screens.end()) {
          removed.push_back(screen);
        }
      }
      s.screenOutputs = std::move(outputs);
      quickshell.screens.set(std::move(screens));
      // QScreen objects of removed outputs go away; windows on them see `destroyed`.
      for (ShellScreen* screen : removed) {
        screen->deleteLater();
      }
    }

  } // namespace

  void setPlatform(WaylandConnection* wayland, RenderContext* render) {
    auto& s = state();
    if (s.wayland != nullptr && s.wayland != wayland) {
      s.wayland->setPointerEventCallback({});
      s.wayland->setOutputChangeCallback({});
    }
    s.wayland = wayland;
    s.render = render;
    setCanvasTextureRenderer(render);
    if (wayland != nullptr) {
      wayland->setPointerEventCallback(routePointer);
      wayland->setOutputChangeCallback(syncScreens);
      setPolishRequestHandler(onPolishRequested);
      AnimationDriver::instance().setFrameRequestHandler(onFrameRequested);
    } else {
      setPolishRequestHandler({});
      AnimationDriver::instance().setFrameRequestHandler({});
      s.fallbackTick.stop();
    }
    syncScreens();
    s.changed.emit();
  }

  WaylandConnection* waylandConnection() { return state().wayland; }
  RenderContext* renderContext() { return state().render; }
  Signal<>& platformChanged() { return state().changed; }

  wl_output* outputForScreen(const ShellScreen* screen) {
    const auto& outputs = state().screenOutputs;
    const auto it = outputs.find(screen);
    return it == outputs.end() ? nullptr : it->second;
  }

  void registerWindow(wl_surface* surface, PlatformWindow window) {
    unregisterWindow(surface);
    state().windows.emplace_back(surface, std::move(window));
  }

  void unregisterWindow(wl_surface* surface) {
    std::erase_if(state().windows, [surface](const auto& entry) { return entry.first == surface; });
  }

} // namespace ii::qs
