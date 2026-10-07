#pragma once

// Quickshell.Hyprland: the IPC singleton (Hyprland), its monitor/workspace/toplevel objects,
// and GlobalShortcut (hyprland-global-shortcuts-v1).

#include "compat/object_model.h"
#include "compat/screen.h"
#include "runtime/fd_watch.h"
#include "runtime/js.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ii::qs {

  class HyprlandMonitor;
  class HyprlandToplevel;

  class HyprlandWorkspace : public Object {
  public:
    HyprlandWorkspace();
    ~HyprlandWorkspace() override;

    Property<int> id{-1};
    Property<std::string> name;
    Property<bool> active;
    Property<bool> focused;
    Property<bool> urgent;
    Property<bool> hasFullscreen;
    Property<js::Json> lastIpcObject;
    Property<HyprlandMonitor*> monitor;
    Property<UntypedObjectModel*> toplevels;

    void activate();

    void updateInitial(int id, const std::string& name);
    void updateFromObject(const js::Json& object);
    void setMonitor(HyprlandMonitor* mon);
    void insertToplevel(HyprlandToplevel* toplevel);
    void removeToplevel(HyprlandToplevel* toplevel);
    void updateUrgent();
    void clearUrgent();

  private:
    Connection m_monitorDestroyedConn;
    std::map<HyprlandToplevel*, Connection> m_toplevelUrgentConns;
    std::map<HyprlandToplevel*, Connection> m_toplevelDestroyedConns;
  };

  class HyprlandMonitor : public Object {
  public:
    HyprlandMonitor();
    ~HyprlandMonitor() override;

    Property<int> id{-1};
    Property<std::string> name;
    Property<std::string> description;
    Property<int> x{0};
    Property<int> y{0};
    Property<int> width{0};
    Property<int> height{0};
    Property<double> scale{1.0};
    Property<js::Json> lastIpcObject;
    Property<HyprlandWorkspace*> activeWorkspace;
    Property<bool> focused;

    void updateInitial(int id, const std::string& name, const std::string& description);
    void updateFromObject(const js::Json& object);
    void setActiveWorkspace(HyprlandWorkspace* workspace);

  private:
    Connection m_activeWsDestroyedConn;
  };

  class HyprlandToplevel : public Object {
  public:
    HyprlandToplevel();
    ~HyprlandToplevel() override;

    Property<std::string> address;
    Property<std::string> title;
    Property<bool> activated;
    Property<bool> urgent;
    Property<js::Json> lastIpcObject;
    Property<HyprlandWorkspace*> workspace;
    Property<HyprlandMonitor*> monitor;

    [[nodiscard]] std::uint64_t rawAddress() const noexcept { return m_rawAddress; }
    void setAddress(std::uint64_t address);
    void setWorkspace(HyprlandWorkspace* workspace);
    void updateInitial(std::uint64_t address, const std::string& title, const std::string& workspaceName);
    void updateFromObject(const js::Json& object);

  private:
    Connection m_workspaceDestroyedConn;
    std::uint64_t m_rawAddress = 0;
  };

  // Hyprland's socket2 events (HyprlandIpcEvent).
  class HyprlandEvent : public Object {
  public:
    Property<std::string> name;
    Property<std::string> data;

    [[nodiscard]] std::vector<std::string> parse(int count) const;
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

    // Internal helpers matching Quickshell connection.hpp
    void setFocusedMonitor(HyprlandMonitor* monitor);
    void setActiveToplevel(HyprlandToplevel* toplevel);
    HyprlandWorkspace* findWorkspaceByName(const std::string& name, bool createIfMissing, int id = -1);
    HyprlandMonitor* findMonitorByName(const std::string& name, bool createIfMissing, int id = -1);
    HyprlandToplevel* findToplevelByAddress(std::uint64_t address, bool createIfMissing);
    void refreshMonitors(bool canCreate);
    void refreshWorkspaces(bool canCreate);

    [[nodiscard]] static std::vector<std::string> parseEventArgs(std::string_view data, int count);

  private:
    Hyprland();
    ~Hyprland() override;

    void onEvent(HyprlandEvent* event);
    void insertWorkspaceSorted(HyprlandWorkspace* workspace);
    void makeRequest(std::string request, std::function<void(bool, std::string)> callback);
    void connectEventSocket();
    void onEventSocketRead();

    struct RequestState;
    void handleRequestPoll(const std::shared_ptr<RequestState>& state, short revents);
    void finishRequest(const std::shared_ptr<RequestState>& state, bool success, std::string response);

    int m_eventFd = -1;
    FdWatch::Id m_eventWatchId = 0;
    std::string m_eventBuffer;
    HyprlandEvent m_event;

    bool m_requestingMonitors = false;
    bool m_requestingWorkspaces = false;
    bool m_requestingToplevels = false;
    bool m_monitorsRequested = false;
    Connection m_focusedMonDestroyedConn;
    Connection m_activeToplevelDestroyedConn;
  };

  struct ManagedShortcut;

  // A global shortcut Hyprland binds as `global, <appid>:<name>`; Quickshell's appid is
  // "quickshell", ii-shell's "iishell".
  // As Quickshell's src/wayland/hyprland/global_shortcuts/qml.hpp
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

    void handlePressed();
    void handleReleased();
    void bindTo(ManagedShortcut* managed, std::string appidVal, std::string nameVal);
    void unbind();
    [[nodiscard]] bool isRegistered() const noexcept { return m_isRegistered; }

  protected:
    void componentComplete() override;

  private:
    void updateRegistration();

    std::string m_registeredAppid;
    std::string m_registeredName;
    bool m_isRegistered = false;
    Connection m_pressedConn;
    Connection m_releasedConn;
  };

} // namespace ii::qs
