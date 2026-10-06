#pragma once

// Quickshell.Hyprland: the IPC singleton (Hyprland), its monitor/workspace/toplevel objects,
// and GlobalShortcut (hyprland-global-shortcuts-v1).

#include "compat/object_model.h"
#include "compat/screen.h"
#include "runtime/js.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <string>

namespace ii::qs {

  class HyprlandMonitor;
  class HyprlandToplevel;

  class HyprlandWorkspace : public Object {
  public:
    Property<int> id;
    Property<std::string> name;
    Property<bool> active;
    Property<bool> focused;
    Property<bool> urgent;
    Property<bool> hasFullscreen;
    Property<js::Json> lastIpcObject;
    Property<HyprlandMonitor*> monitor;
    Property<UntypedObjectModel*> toplevels;

    void activate();
  };

  class HyprlandMonitor : public Object {
  public:
    Property<int> id;
    Property<std::string> name;
    Property<std::string> description;
    Property<int> x;
    Property<int> y;
    Property<int> width;
    Property<int> height;
    Property<double> scale{1.0};
    Property<js::Json> lastIpcObject;
    Property<HyprlandWorkspace*> activeWorkspace;
    Property<bool> focused;
  };

  class HyprlandToplevel : public Object {
  public:
    Property<std::string> address;
    Property<std::string> title;
    Property<bool> activated;
    Property<bool> urgent;
    Property<js::Json> lastIpcObject;
    Property<HyprlandWorkspace*> workspace;
    Property<HyprlandMonitor*> monitor;
  };

  // Hyprland's socket2 events (HyprlandIpcEvent).
  class HyprlandEvent : public Object {
  public:
    Property<std::string> name;
    Property<std::string> data;
  };

  class Hyprland : public Object {
  public:
    static Hyprland& instance();

    Property<std::string> requestSocketPath;
    Property<std::string> eventSocketPath;
    Property<HyprlandMonitor*> focusedMonitor;
    Property<HyprlandWorkspace*> focusedWorkspace;
    Property<HyprlandToplevel*> activeToplevel;
    Property<UntypedObjectModel*> monitors;
    Property<UntypedObjectModel*> workspaces;
    Property<UntypedObjectModel*> toplevels;
    Signal<HyprlandEvent*> rawEvent;

    void dispatch(const std::string& request);
    [[nodiscard]] HyprlandMonitor* monitorFor(ShellScreen* screen) const;
    void refreshMonitors();
    void refreshWorkspaces();
    void refreshToplevels();

  private:
    Hyprland();
  };

  // A global shortcut Hyprland binds as `global, <appid>:<name>`; Quickshell's appid is
  // "quickshell", ii-shell's "iishell".
  class GlobalShortcut : public Object {
  public:
    GlobalShortcut();
    ~GlobalShortcut() override;

    Property<bool> pressed;  // read-only
    Property<std::string> appid{"iishell"};
    Property<std::string> name;
    Property<std::string> description;
    Property<std::string> triggerDescription;
    Signal<> pressedSignal;
    Signal<> released;

  protected:
    void componentComplete() override;
  };

} // namespace ii::qs
