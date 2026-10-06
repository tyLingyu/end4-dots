#include "compat/hyprland.h"

#include "core/log.h"

namespace ii::qs {

  namespace {
    constexpr Logger kLog("hyprland");
  }

  // The IPC connection (request socket, socket2 events) comes in stage 3b; until then the
  // singleton exposes empty models and no focused monitor.
  Hyprland& Hyprland::instance() {
    static Hyprland* self = [] {
      auto* h = new Hyprland();
      h->complete();
      return h;
    }();
    return *self;
  }

  Hyprland::Hyprland() {
    monitors.set(create<UntypedObjectModel>());
    workspaces.set(create<UntypedObjectModel>());
    toplevels.set(create<UntypedObjectModel>());
  }

  void Hyprland::dispatch(const std::string& request) { kLog.debug("dispatch {} (IPC not connected yet)", request); }

  HyprlandMonitor* Hyprland::monitorFor(ShellScreen* screen) const {
    if (screen == nullptr) {
      return nullptr;
    }
    for (Object* object : monitors.peek()->values.peek()) {
      auto* monitor = static_cast<HyprlandMonitor*>(object);
      if (monitor->name.peek() == screen->name.peek()) {
        return monitor;
      }
    }
    return nullptr;
  }

  void Hyprland::refreshMonitors() {}
  void Hyprland::refreshWorkspaces() {}
  void Hyprland::refreshToplevels() {}

  void HyprlandWorkspace::activate() {
    Hyprland::instance().dispatch("workspace " + std::to_string(id.peek()));
  }

  GlobalShortcut::GlobalShortcut() = default;
  GlobalShortcut::~GlobalShortcut() = default;

  void GlobalShortcut::componentComplete() {
    kLog.debug("global shortcut {}:{} (registration comes in stage 3b)", appid.peek(), name.peek());
  }

} // namespace ii::qs
