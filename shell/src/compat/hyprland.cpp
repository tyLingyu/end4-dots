#include "compat/hyprland.h"

#include "core/deferred_call.h"
#include "core/log.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace ii::qs {

  namespace {
    constexpr Logger kLog("hyprland");

    std::optional<std::uint64_t> parseAddressOpt(std::string_view s) {
      if (s.starts_with("0x") || s.starts_with("0X")) {
        s.remove_prefix(2);
      }
      if (s.empty()) {
        return std::nullopt;
      }
      std::uint64_t val = 0;
      auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val, 16);
      if (ec == std::errc{} && ptr == s.data() + s.size()) {
        return val;
      }
      return std::nullopt;
    }

    std::uint64_t parseAddress(std::string_view s) {
      return parseAddressOpt(s).value_or(0);
    }

    int jsonGetInt(const js::Json& j, std::string_view key, int fallback = 0) {
      if (j.is_object() && j.contains(std::string(key))) {
        const auto& val = j[std::string(key)];
        if (val.is_number_integer()) {
          return val.get<int>();
        }
        if (val.is_number()) {
          return static_cast<int>(val.get<double>());
        }
      }
      return fallback;
    }

    double jsonGetDouble(const js::Json& j, std::string_view key, double fallback = 0.0) {
      if (j.is_object() && j.contains(std::string(key))) {
        const auto& val = j[std::string(key)];
        if (val.is_number()) {
          return val.get<double>();
        }
      }
      return fallback;
    }

    std::string jsonGetString(const js::Json& j, std::string_view key, std::string fallback = "") {
      if (j.is_object() && j.contains(std::string(key))) {
        const auto& val = j[std::string(key)];
        if (val.is_string()) {
          return val.get<std::string>();
        }
      }
      return fallback;
    }

    bool jsonGetBool(const js::Json& j, std::string_view key, bool fallback = false) {
      if (j.is_object() && j.contains(std::string(key))) {
        const auto& val = j[std::string(key)];
        if (val.is_boolean()) {
          return val.get<bool>();
        }
      }
      return fallback;
    }
  } // namespace

  // ── HyprlandEvent ───────────────────────────────────────────────────────────
  // as Quickshell's src/wayland/hyprland/ipc/connection.cpp

  std::vector<std::string> HyprlandEvent::parse(int count) const {
    return Hyprland::parseEventArgs(data.peek(), count);
  }

  // ── HyprlandMonitor ─────────────────────────────────────────────────────────
  // as Quickshell's src/wayland/hyprland/ipc/monitor.cpp

  HyprlandMonitor::HyprlandMonitor() {
    focused.bind([this] {
      return Hyprland::instance().focusedMonitor.get() == this;
    });
  }

  HyprlandMonitor::~HyprlandMonitor() {
    m_activeWsDestroyedConn.disconnect();
    if (Hyprland::instance().focusedMonitor.peek() == this) {
      Hyprland::instance().setFocusedMonitor(nullptr);
    }
    const auto wsList = Hyprland::instance().workspaces.peek()->values.peek();
    for (Object* obj : wsList) {
      auto* ws = static_cast<HyprlandWorkspace*>(obj);
      if (ws->monitor.peek() == this) {
        ws->setMonitor(nullptr);
      }
    }
  }

  void HyprlandMonitor::updateInitial(int idVal, const std::string& nameVal, const std::string& descVal) {
    id.set(idVal);
    name.set(nameVal);
    description.set(descVal);
  }

  void HyprlandMonitor::updateFromObject(const js::Json& object) {
    id.set(jsonGetInt(object, "id", -1));
    name.set(jsonGetString(object, "name"));
    description.set(jsonGetString(object, "description"));
    x.set(jsonGetInt(object, "x"));
    y.set(jsonGetInt(object, "y"));
    width.set(jsonGetInt(object, "width"));
    height.set(jsonGetInt(object, "height"));
    scale.set(jsonGetDouble(object, "scale", 1.0));

    std::string activeWsName;
    int activeWsId = -1;
    if (object.contains("activeWorkspace") && object["activeWorkspace"].is_object()) {
      activeWsId = jsonGetInt(object["activeWorkspace"], "id", -1);
      activeWsName = jsonGetString(object["activeWorkspace"], "name");
    }

    if (activeWorkspace.peek() == nullptr || activeWorkspace.peek()->name.peek() != activeWsName) {
      auto* ws = Hyprland::instance().findWorkspaceByName(activeWsName, true, activeWsId);
      if (ws != nullptr) {
        ws->setMonitor(this);
      }
      setActiveWorkspace(ws);
    }

    lastIpcObject.set(object);

    if (jsonGetBool(object, "focused", false)) {
      Hyprland::instance().setFocusedMonitor(this);
    }
  }

  void HyprlandMonitor::setActiveWorkspace(HyprlandWorkspace* workspace) {
    if (workspace == activeWorkspace.peek()) {
      return;
    }
    m_activeWsDestroyedConn.disconnect();
    if (workspace != nullptr) {
      workspace->setMonitor(this);
      m_activeWsDestroyedConn = workspace->destroyed.connect([this] {
        activeWorkspace.set(nullptr);
      });
    }
    activeWorkspace.set(workspace);
  }

  // ── HyprlandWorkspace ───────────────────────────────────────────────────────
  // as Quickshell's src/wayland/hyprland/ipc/workspace.cpp

  HyprlandWorkspace::HyprlandWorkspace() {
    toplevels.set(create<UntypedObjectModel>());

    active.bind([this]() -> bool {
      HyprlandMonitor* mon = monitor.get();
      return mon != nullptr && mon->activeWorkspace.get() == this;
    });

    focused.bind([this]() -> bool {
      return Hyprland::instance().focusedWorkspace.get() == this;
    });

    focused.changed().connectForever([this] {
      if (focused.peek()) {
        updateUrgent();
      }
    });
  }

  HyprlandWorkspace::~HyprlandWorkspace() {
    m_monitorDestroyedConn.disconnect();
    for (auto& [_, conn] : m_toplevelUrgentConns) {
      conn.disconnect();
    }
    m_toplevelUrgentConns.clear();
    for (auto& [_, conn] : m_toplevelDestroyedConns) {
      conn.disconnect();
    }
    m_toplevelDestroyedConns.clear();

    if (auto* model = toplevels.peek()) {
      for (Object* obj : model->values.peek()) {
        auto* top = static_cast<HyprlandToplevel*>(obj);
        if (top->workspace.peek() == this) {
          top->setWorkspace(nullptr);
        }
      }
    }

    for (Object* obj : Hyprland::instance().monitors.peek()->values.peek()) {
      auto* mon = static_cast<HyprlandMonitor*>(obj);
      if (mon->activeWorkspace.peek() == this) {
        mon->setActiveWorkspace(nullptr);
      }
    }
  }

  void HyprlandWorkspace::activate() {
    const std::string& n = name.peek();
    Hyprland::instance().dispatch("workspace " + (n.empty() ? std::to_string(id.peek()) : n));
  }

  void HyprlandWorkspace::updateInitial(int idVal, const std::string& nameVal) {
    id.set(idVal);
    name.set(nameVal);
  }

  void HyprlandWorkspace::updateFromObject(const js::Json& object) {
    const int monId = jsonGetInt(object, "monitorID", -1);
    const std::string monName = jsonGetString(object, "monitor");
    const bool hasFs = jsonGetBool(object, "hasfullscreen", false);

    if (id.peek() == -1) {
      id.set(jsonGetInt(object, "id", -1));
      name.set(jsonGetString(object, "name"));
    }

    if (!monName.empty() && (monitor.peek() == nullptr || monitor.peek()->name.peek() != monName)) {
      auto* mon = Hyprland::instance().findMonitorByName(monName, true, monId);
      setMonitor(mon);
    }

    hasFullscreen.set(hasFs);
    lastIpcObject.set(object);
  }

  void HyprlandWorkspace::setMonitor(HyprlandMonitor* mon) {
    if (mon == monitor.peek()) {
      return;
    }
    m_monitorDestroyedConn.disconnect();
    if (mon != nullptr) {
      m_monitorDestroyedConn = mon->destroyed.connect([this] {
        monitor.set(nullptr);
      });
    }
    monitor.set(mon);
  }

  void HyprlandWorkspace::insertToplevel(HyprlandToplevel* toplevel) {
    if (toplevel == nullptr) {
      return;
    }
    auto* model = toplevels.peek();
    if (!model || model->indexOf(toplevel) != -1) {
      return;
    }
    model->insertObject(toplevel);
    m_toplevelUrgentConns[toplevel] = toplevel->urgent.changed().connect([this] {
      updateUrgent();
    });
    m_toplevelDestroyedConns[toplevel] = toplevel->destroyed.connect([this, toplevel] {
      removeToplevel(toplevel);
    });
    updateUrgent();
  }

  void HyprlandWorkspace::removeToplevel(HyprlandToplevel* toplevel) {
    if (toplevel == nullptr) {
      return;
    }
    auto* model = toplevels.peek();
    if (!model) {
      return;
    }
    model->removeObject(toplevel);
    if (auto it = m_toplevelUrgentConns.find(toplevel); it != m_toplevelUrgentConns.end()) {
      it->second.disconnect();
      m_toplevelUrgentConns.erase(it);
    }
    if (auto it = m_toplevelDestroyedConns.find(toplevel); it != m_toplevelDestroyedConns.end()) {
      it->second.disconnect();
      m_toplevelDestroyedConns.erase(it);
    }
    updateUrgent();
  }

  void HyprlandWorkspace::updateUrgent() {
    auto* model = toplevels.peek();
    if (!model) {
      return;
    }
    const auto& list = model->values.peek();
    const bool hasUrgent = std::ranges::any_of(list, [](Object* obj) {
      auto* top = static_cast<HyprlandToplevel*>(obj);
      return top->urgent.peek();
    });

    if (focused.peek() && hasUrgent) {
      clearUrgent();
      return;
    }

    if (hasUrgent != urgent.peek()) {
      urgent.set(hasUrgent);
    }
  }

  void HyprlandWorkspace::clearUrgent() {
    urgent.set(false);
    auto* model = toplevels.peek();
    if (!model) {
      return;
    }
    for (Object* obj : model->values.peek()) {
      auto* top = static_cast<HyprlandToplevel*>(obj);
      top->urgent.set(false);
    }
  }

  // ── HyprlandToplevel ────────────────────────────────────────────────────────
  // as Quickshell's src/wayland/hyprland/ipc/hyprland_toplevel.cpp

  HyprlandToplevel::HyprlandToplevel() {
    monitor.bind([this]() -> HyprlandMonitor* {
      HyprlandWorkspace* ws = workspace.get();
      return ws != nullptr ? ws->monitor.get() : nullptr;
    });

    activated.bind([this]() -> bool {
      return Hyprland::instance().activeToplevel.get() == this;
    });

    activated.changed().connectForever([this] {
      if (activated.peek() && urgent.peek()) {
        urgent.set(false);
      }
    });
  }

  HyprlandToplevel::~HyprlandToplevel() {
    m_workspaceDestroyedConn.disconnect();
    if (Hyprland::instance().activeToplevel.peek() == this) {
      Hyprland::instance().setActiveToplevel(nullptr);
    }
    for (Object* obj : Hyprland::instance().workspaces.peek()->values.peek()) {
      auto* ws = static_cast<HyprlandWorkspace*>(obj);
      ws->removeToplevel(this);
    }
  }

  void HyprlandToplevel::setAddress(std::uint64_t addr) {
    m_rawAddress = addr;
    address.set(addr == 0 ? std::string{} : std::format("{:x}", addr));
  }

  void HyprlandToplevel::setWorkspace(HyprlandWorkspace* ws) {
    if (ws == workspace.peek()) {
      return;
    }
    m_workspaceDestroyedConn.disconnect();
    if (ws != nullptr) {
      m_workspaceDestroyedConn = ws->destroyed.connect([this] {
        workspace.set(nullptr);
      });
    }
    workspace.set(ws);
  }

  void HyprlandToplevel::updateInitial(std::uint64_t addr, const std::string& titleVal, const std::string& workspaceName) {
    setAddress(addr);
    title.set(titleVal);
    if (!workspaceName.empty()) {
      auto* ws = Hyprland::instance().findWorkspaceByName(workspaceName, false);
      setWorkspace(ws);
    }
  }

  void HyprlandToplevel::updateFromObject(const js::Json& object) {
    const std::string addrStr = jsonGetString(object, "address");
    const std::uint64_t addr = parseAddress(addrStr);
    if (addr != 0) {
      setAddress(addr);
    }
    title.set(jsonGetString(object, "title"));

    std::string wsName;
    if (object.contains("workspace") && object["workspace"].is_object()) {
      wsName = jsonGetString(object["workspace"], "name");
    }
    if (!wsName.empty()) {
      auto* ws = Hyprland::instance().findWorkspaceByName(wsName, true);
      if (ws != nullptr) {
        setWorkspace(ws);
      }
    }
    lastIpcObject.set(object);
  }

  // ── Hyprland ────────────────────────────────────────────────────────────────
  // as Quickshell's src/wayland/hyprland/ipc/connection.cpp

  struct Hyprland::RequestState {
    int fd = -1;
    FdWatch::Id watchId = 0;
    std::string request;
    std::size_t written = 0;
    std::string response;
    std::function<void(bool, std::string)> callback;
    bool writing = true;
  };

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

    focusedWorkspace.bind([this]() -> HyprlandWorkspace* {
      HyprlandMonitor* mon = focusedMonitor.get();
      return mon != nullptr ? mon->activeWorkspace.get() : nullptr;
    });

    const char* his = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!his || his[0] == '\0') {
      kLog.warn("$HYPRLAND_INSTANCE_SIGNATURE is unset. Cannot connect to hyprland.");
      return;
    }

    const std::string hisStr = his;
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    std::string hyprlandDir = (xdg && xdg[0] != '\0') ? (std::string(xdg) + "/hypr/" + hisStr) : "";

    std::error_code ec;
    if (hyprlandDir.empty() || !std::filesystem::is_directory(hyprlandDir, ec)) {
      hyprlandDir = "/tmp/hypr/" + hisStr;
    }

    if (!std::filesystem::is_directory(hyprlandDir, ec)) {
      kLog.warn("Unable to find hyprland socket. Cannot connect to hyprland.");
      return;
    }

    requestSocketPath.set(hyprlandDir + "/.socket.sock");
    eventSocketPath.set(hyprlandDir + "/.socket2.sock");

    connectEventSocket();

    refreshMonitors(true);
    refreshWorkspaces(true);
    refreshToplevels();
  }

  Hyprland::~Hyprland() {
    if (m_eventWatchId != 0) {
      FdWatch::unwatch(m_eventWatchId);
      m_eventWatchId = 0;
    }
    if (m_eventFd >= 0) {
      ::close(m_eventFd);
      m_eventFd = -1;
    }
  }

  void Hyprland::connectEventSocket() {
    const std::string& path = eventSocketPath.peek();
    if (path.empty()) {
      return;
    }

    m_eventFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_eventFd < 0) {
      kLog.warn("Unable to create hyprland event socket: errno={}", errno);
      return;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
      kLog.warn("Event socket path too long: {}", path);
      ::close(m_eventFd);
      m_eventFd = -1;
      return;
    }
    std::memcpy(addr.sun_path, path.data(), path.size());
    addr.sun_path[path.size()] = '\0';

    const int rc = ::connect(m_eventFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
      kLog.warn("Unable to connect to hyprland event socket: errno={}", errno);
      ::close(m_eventFd);
      m_eventFd = -1;
      return;
    }

    m_eventWatchId = FdWatch::watch(m_eventFd, POLLIN, [this](short revents) {
      if ((revents & (POLLERR | POLLNVAL)) != 0) {
        kLog.warn("Hyprland event socket error");
        if (m_eventWatchId != 0) {
          FdWatch::unwatch(m_eventWatchId);
          m_eventWatchId = 0;
        }
        if (m_eventFd >= 0) {
          ::close(m_eventFd);
          m_eventFd = -1;
        }
        return;
      }
      onEventSocketRead();
    });
  }

  void Hyprland::onEventSocketRead() {
    char buf[4096];
    while (true) {
      const ssize_t n = ::read(m_eventFd, buf, sizeof(buf));
      if (n > 0) {
        m_eventBuffer.append(buf, static_cast<std::size_t>(n));
      } else if (n == 0) {
        kLog.warn("Hyprland event socket disconnected");
        if (m_eventWatchId != 0) {
          FdWatch::unwatch(m_eventWatchId);
          m_eventWatchId = 0;
        }
        if (m_eventFd >= 0) {
          ::close(m_eventFd);
          m_eventFd = -1;
        }
        return;
      } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        }
        kLog.warn("Hyprland event socket read error: errno={}", errno);
        if (m_eventWatchId != 0) {
          FdWatch::unwatch(m_eventWatchId);
          m_eventWatchId = 0;
        }
        if (m_eventFd >= 0) {
          ::close(m_eventFd);
          m_eventFd = -1;
        }
        return;
      }
    }

    while (true) {
      const auto nlPos = m_eventBuffer.find('\n');
      if (nlPos == std::string::npos) {
        break;
      }
      std::string line = m_eventBuffer.substr(0, nlPos);
      m_eventBuffer.erase(0, nlPos + 1);

      const auto splitPos = line.find(">>");
      if (splitPos == std::string::npos) {
        continue;
      }
      std::string evName = line.substr(0, splitPos);
      std::string evData = line.substr(splitPos + 2);

      m_event.name.set(std::move(evName));
      m_event.data.set(std::move(evData));

      onEvent(&m_event);
      rawEvent.emit(&m_event);
    }
  }

  void Hyprland::finishRequest(const std::shared_ptr<RequestState>& state, bool success, std::string response) {
    if (state->watchId != 0) {
      FdWatch::unwatch(state->watchId);
      state->watchId = 0;
    }
    if (state->fd >= 0) {
      ::close(state->fd);
      state->fd = -1;
    }
    if (state->callback) {
      auto cb = std::move(state->callback);
      cb(success, std::move(response));
    }
  }

  void Hyprland::handleRequestPoll(const std::shared_ptr<RequestState>& state, short revents) {
    if (state->writing) {
      if ((revents & (POLLERR | POLLNVAL)) != 0) {
        finishRequest(state, false, {});
        return;
      }

      int err = 0;
      socklen_t len = sizeof(err);
      if (getsockopt(state->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
        finishRequest(state, false, {});
        return;
      }

      while (state->written < state->request.size()) {
        const ssize_t n = ::send(state->fd, state->request.data() + state->written, state->request.size() - state->written, MSG_NOSIGNAL);
        if (n > 0) {
          state->written += static_cast<std::size_t>(n);
        } else if (n < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
          }
          finishRequest(state, false, {});
          return;
        } else {
          break;
        }
      }

      if (state->written >= state->request.size()) {
        state->writing = false;
        FdWatch::setEvents(state->watchId, POLLIN);
      }
    }

    if (!state->writing) {
      char buf[4096];
      while (true) {
        const ssize_t n = ::read(state->fd, buf, sizeof(buf));
        if (n > 0) {
          state->response.append(buf, static_cast<std::size_t>(n));
        } else if (n == 0) {
          finishRequest(state, true, std::move(state->response));
          return;
        } else {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
          }
          finishRequest(state, false, {});
          return;
        }
      }

      if ((revents & POLLHUP) != 0) {
        finishRequest(state, true, std::move(state->response));
        return;
      }
    }
  }

  void Hyprland::makeRequest(std::string request, std::function<void(bool, std::string)> callback) {
    const std::string& path = requestSocketPath.peek();
    if (path.empty()) {
      if (callback) {
        callback(false, {});
      }
      return;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
      kLog.warn("Failed to create request socket: errno={}", errno);
      if (callback) {
        callback(false, {});
      }
      return;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
      kLog.warn("Request socket path too long: {}", path);
      ::close(fd);
      if (callback) {
        callback(false, {});
      }
      return;
    }
    std::memcpy(addr.sun_path, path.data(), path.size());
    addr.sun_path[path.size()] = '\0';

    const int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
      kLog.warn("Failed to connect to request socket: errno={}", errno);
      ::close(fd);
      if (callback) {
        callback(false, {});
      }
      return;
    }

    auto state = std::make_shared<RequestState>();
    state->fd = fd;
    state->request = std::move(request);
    state->callback = std::move(callback);

    state->watchId = FdWatch::watch(fd, static_cast<short>(POLLOUT | POLLIN), [this, state](short revents) {
      handleRequestPoll(state, revents);
    });
  }

  void Hyprland::dispatch(const std::string& request) {
    makeRequest("dispatch " + request, [request](bool success, const std::string& response) {
      if (!success) {
        kLog.warn("Failed to request dispatch of {}", request);
        return;
      }
      if (response != "ok") {
        kLog.warn("Dispatch request {} failed with error {}", request, response);
      }
    });
  }

  HyprlandMonitor* Hyprland::monitorFor(ShellScreen* screen) const {
    if (screen == nullptr) {
      return nullptr;
    }
    return const_cast<Hyprland*>(this)->findMonitorByName(screen->name.peek(), !m_monitorsRequested);
  }

  void Hyprland::insertWorkspaceSorted(HyprlandWorkspace* workspace) {
    const auto& list = workspaces.peek()->values.peek();
    int idx = 0;
    while (idx < static_cast<int>(list.size())) {
      auto* other = static_cast<HyprlandWorkspace*>(list[static_cast<std::size_t>(idx)]);
      if (workspace->id.peek() <= other->id.peek()) {
        break;
      }
      ++idx;
    }
    workspaces.peek()->insertObject(workspace, idx);
  }

  void Hyprland::setFocusedMonitor(HyprlandMonitor* monitor) {
    if (focusedMonitor.peek() == monitor) {
      return;
    }
    m_focusedMonDestroyedConn.disconnect();
    if (monitor != nullptr) {
      m_focusedMonDestroyedConn = monitor->destroyed.connect([this] {
        focusedMonitor.set(nullptr);
      });
    }
    focusedMonitor.set(monitor);
  }

  void Hyprland::setActiveToplevel(HyprlandToplevel* toplevel) {
    if (activeToplevel.peek() == toplevel) {
      return;
    }
    m_activeToplevelDestroyedConn.disconnect();
    if (toplevel != nullptr) {
      m_activeToplevelDestroyedConn = toplevel->destroyed.connect([this] {
        activeToplevel.set(nullptr);
      });
    }
    activeToplevel.set(toplevel);
  }

  HyprlandWorkspace* Hyprland::findWorkspaceByName(const std::string& name, bool createIfMissing, int id) {
    const auto& list = workspaces.peek()->values.peek();
    HyprlandWorkspace* workspace = nullptr;

    if (id != -1) {
      for (Object* obj : list) {
        auto* ws = static_cast<HyprlandWorkspace*>(obj);
        if (ws->id.peek() == id) {
          workspace = ws;
          break;
        }
      }
    }

    if (!workspace) {
      for (Object* obj : list) {
        auto* ws = static_cast<HyprlandWorkspace*>(obj);
        if (ws->name.peek() == name) {
          workspace = ws;
          break;
        }
      }
    }

    if (workspace != nullptr) {
      return workspace;
    }

    if (createIfMissing) {
      workspace = create<HyprlandWorkspace>();
      workspace->updateInitial(id, name);
      insertWorkspaceSorted(workspace);
      return workspace;
    }

    return nullptr;
  }

  HyprlandMonitor* Hyprland::findMonitorByName(const std::string& name, bool createIfMissing, int id) {
    const auto& list = monitors.peek()->values.peek();
    for (Object* obj : list) {
      auto* mon = static_cast<HyprlandMonitor*>(obj);
      if (mon->name.peek() == name) {
        return mon;
      }
    }

    if (createIfMissing) {
      auto* mon = create<HyprlandMonitor>();
      mon->updateInitial(id, name, "");
      monitors.peek()->insertObject(mon);
      return mon;
    }

    return nullptr;
  }

  HyprlandToplevel* Hyprland::findToplevelByAddress(std::uint64_t address, bool createIfMissing) {
    const auto& list = toplevels.peek()->values.peek();
    for (Object* obj : list) {
      auto* top = static_cast<HyprlandToplevel*>(obj);
      if (top->rawAddress() == address) {
        return top;
      }
    }

    if (createIfMissing) {
      auto* top = create<HyprlandToplevel>();
      top->updateInitial(address, "", "");
      toplevels.peek()->insertObject(top);
      return top;
    }

    return nullptr;
  }

  void Hyprland::refreshWorkspaces() {
    refreshWorkspaces(false);
  }

  void Hyprland::refreshWorkspaces(bool canCreate) {
    if (m_requestingWorkspaces) {
      return;
    }
    m_requestingWorkspaces = true;

    makeRequest("j/workspaces", [this, canCreate](bool success, std::string resp) {
      m_requestingWorkspaces = false;
      if (!success) {
        return;
      }

      js::Json json;
      try {
        json = js::Json::parse(resp);
      } catch (const std::exception& e) {
        kLog.warn("Failed to parse j/workspaces: {}", e.what());
        return;
      }
      if (!json.is_array()) {
        return;
      }

      const auto initialList = workspaces.peek()->values.peek();
      std::vector<int> ids;

      for (const auto& entry : json) {
        if (!entry.is_object()) {
          continue;
        }
        const int idVal = jsonGetInt(entry, "id", -1);
        const std::string nameVal = jsonGetString(entry, "name");

        const auto currentList = workspaces.peek()->values.peek();
        HyprlandWorkspace* workspace = nullptr;

        for (Object* obj : currentList) {
          auto* ws = static_cast<HyprlandWorkspace*>(obj);
          if (ws->id.peek() == idVal) {
            workspace = ws;
            break;
          }
        }

        if (!workspace) {
          for (Object* obj : currentList) {
            auto* ws = static_cast<HyprlandWorkspace*>(obj);
            if (ws->id.peek() == -1 && ws->name.peek() == nameVal) {
              workspace = ws;
              break;
            }
          }
        }

        const bool existed = (workspace != nullptr);
        if (!existed) {
          if (!canCreate) {
            continue;
          }
          workspace = create<HyprlandWorkspace>();
        }

        workspace->updateFromObject(entry);

        if (!existed) {
          insertWorkspaceSorted(workspace);
        }

        ids.push_back(idVal);
      }

      if (canCreate) {
        std::vector<HyprlandWorkspace*> removed;
        for (Object* obj : initialList) {
          auto* ws = static_cast<HyprlandWorkspace*>(obj);
          if (std::find(ids.begin(), ids.end(), ws->id.peek()) == ids.end()) {
            removed.push_back(ws);
          }
        }
        for (auto* ws : removed) {
          workspaces.peek()->removeObject(ws);
          ws->deleteLater();
        }
      }
    });
  }

  void Hyprland::refreshMonitors() {
    refreshMonitors(false);
  }

  void Hyprland::refreshMonitors(bool canCreate) {
    if (m_requestingMonitors) {
      return;
    }
    m_requestingMonitors = true;

    makeRequest("j/monitors", [this, canCreate](bool success, std::string resp) {
      m_requestingMonitors = false;
      if (!success) {
        return;
      }
      m_monitorsRequested = true;

      js::Json json;
      try {
        json = js::Json::parse(resp);
      } catch (const std::exception& e) {
        kLog.warn("Failed to parse j/monitors: {}", e.what());
        return;
      }
      if (!json.is_array()) {
        return;
      }

      const auto initialList = monitors.peek()->values.peek();
      std::vector<std::string> names;

      for (const auto& entry : json) {
        if (!entry.is_object()) {
          continue;
        }
        const std::string nameVal = jsonGetString(entry, "name");

        const auto currentList = monitors.peek()->values.peek();
        HyprlandMonitor* monitor = nullptr;
        for (Object* obj : currentList) {
          auto* mon = static_cast<HyprlandMonitor*>(obj);
          if (mon->name.peek() == nameVal) {
            monitor = mon;
            break;
          }
        }

        const bool existed = (monitor != nullptr);
        if (!existed) {
          if (!canCreate) {
            continue;
          }
          monitor = create<HyprlandMonitor>();
        }

        monitor->updateFromObject(entry);

        if (!existed) {
          monitors.peek()->insertObject(monitor);
        }

        names.push_back(nameVal);
      }

      // Prune stale monitors unconditionally on every j/monitors reply (as Quickshell does)
      std::vector<HyprlandMonitor*> removed;
      for (Object* obj : initialList) {
        auto* mon = static_cast<HyprlandMonitor*>(obj);
        if (std::find(names.begin(), names.end(), mon->name.peek()) == names.end()) {
          removed.push_back(mon);
        }
      }
      for (auto* mon : removed) {
        monitors.peek()->removeObject(mon);
        mon->deleteLater();
      }
    });
  }

  void Hyprland::refreshToplevels() {
    if (m_requestingToplevels) {
      return;
    }
    m_requestingToplevels = true;

    makeRequest("j/clients", [this](bool success, std::string resp) {
      m_requestingToplevels = false;
      if (!success) {
        return;
      }

      js::Json json;
      try {
        json = js::Json::parse(resp);
      } catch (const std::exception& e) {
        kLog.warn("Failed to parse j/clients: {}", e.what());
        return;
      }
      if (!json.is_array()) {
        return;
      }

      for (const auto& entry : json) {
        if (!entry.is_object()) {
          continue;
        }
        const std::string addrStr = jsonGetString(entry, "address");
        const std::uint64_t addr = parseAddress(addrStr);
        if (addr == 0) {
          continue;
        }

        const auto currentList = toplevels.peek()->values.peek();
        HyprlandToplevel* toplevel = nullptr;
        for (Object* obj : currentList) {
          auto* top = static_cast<HyprlandToplevel*>(obj);
          if (top->rawAddress() == addr) {
            toplevel = top;
            break;
          }
        }

        const bool existed = (toplevel != nullptr);
        if (!existed) {
          toplevel = create<HyprlandToplevel>();
        }

        toplevel->updateFromObject(entry);

        if (!existed) {
          toplevels.peek()->insertObject(toplevel);
        }

        if (HyprlandWorkspace* ws = toplevel->workspace.peek(); ws != nullptr) {
          ws->insertToplevel(toplevel);
        }
      }
    });
  }

  void Hyprland::onEvent(HyprlandEvent* event) {
    const std::string& evName = event->name.peek();
    const std::string& evData = event->data.peek();

    if (evName == "configreloaded") {
      refreshMonitors(true);
      refreshWorkspaces(true);
      refreshToplevels();
    } else if (evName == "monitoraddedv2") {
      const auto args = event->parse(3);
      const int idVal = std::atoi(args[0].c_str());
      const std::string& nameVal = args[1];
      const std::string& descVal = args[2];

      auto* mon = findMonitorByName(nameVal, false);
      const bool existed = (mon != nullptr);
      if (!mon) {
        mon = create<HyprlandMonitor>();
      }
      mon->updateInitial(idVal, nameVal, descVal);
      if (!existed) {
        monitors.peek()->insertObject(mon);
      }
      refreshMonitors(false);
    } else if (evName == "monitorremoved") {
      const std::string& nameVal = evData;
      const auto list = monitors.peek()->values.peek();
      for (Object* obj : list) {
        auto* mon = static_cast<HyprlandMonitor*>(obj);
        if (mon->name.peek() == nameVal) {
          monitors.peek()->removeObject(mon);
          mon->deleteLater();
          break;
        }
      }
    } else if (evName == "createworkspacev2") {
      const auto args = event->parse(2);
      const int idVal = std::atoi(args[0].c_str());
      const std::string& nameVal = args[1];

      auto* ws = findWorkspaceByName(nameVal, false);
      const bool existed = (ws != nullptr);
      if (!ws) {
        ws = create<HyprlandWorkspace>();
      }
      ws->updateInitial(idVal, nameVal);
      if (!existed) {
        refreshWorkspaces(false);
        insertWorkspaceSorted(ws);
      }
    } else if (evName == "destroyworkspacev2") {
      const auto args = event->parse(2);
      const int idVal = std::atoi(args[0].c_str());

      const auto list = workspaces.peek()->values.peek();
      for (Object* obj : list) {
        auto* ws = static_cast<HyprlandWorkspace*>(obj);
        if (ws->id.peek() == idVal) {
          // Clear activeWorkspace pointing at ws before removal & null check
          for (Object* mObj : monitors.peek()->values.peek()) {
            auto* mon = static_cast<HyprlandMonitor*>(mObj);
            if (mon->activeWorkspace.peek() == ws) {
              mon->setActiveWorkspace(nullptr);
            }
          }
          workspaces.peek()->removeObject(ws);
          ws->deleteLater();
          break;
        }
      }

      for (Object* obj : monitors.peek()->values.peek()) {
        auto* mon = static_cast<HyprlandMonitor*>(obj);
        if (mon->activeWorkspace.peek() == nullptr) {
          refreshMonitors(false);
          break;
        }
      }
    } else if (evName == "focusedmon") {
      const auto args = event->parse(2);
      const std::string& monName = args[0];
      const std::string& wsName = args[1];

      HyprlandWorkspace* ws = nullptr;
      if (wsName != "?") {
        ws = findWorkspaceByName(wsName, false);
      }
      auto* mon = findMonitorByName(monName, true);
      setFocusedMonitor(mon);
      if (mon != nullptr) {
        mon->setActiveWorkspace(ws);
      }
    } else if (evName == "workspacev2") {
      const auto args = event->parse(2);
      const int idVal = std::atoi(args[0].c_str());
      const std::string& nameVal = args[1];

      if (HyprlandMonitor* mon = focusedMonitor.peek(); mon != nullptr) {
        auto* ws = findWorkspaceByName(nameVal, true, idVal);
        mon->setActiveWorkspace(ws);
      }
    } else if (evName == "moveworkspacev2") {
      const auto args = event->parse(3);
      const int idVal = std::atoi(args[0].c_str());
      const std::string& wsName = args[1];
      const std::string& monName = args[2];

      auto* ws = findWorkspaceByName(wsName, true, idVal);
      auto* mon = findMonitorByName(monName, true);
      if (ws != nullptr) {
        ws->setMonitor(mon);
      }
    } else if (evName == "renameworkspace") {
      const auto args = event->parse(2);
      const int idVal = std::atoi(args[0].c_str());
      const std::string& newName = args[1];

      const auto list = workspaces.peek()->values.peek();
      for (Object* obj : list) {
        auto* ws = static_cast<HyprlandWorkspace*>(obj);
        if (ws->id.peek() == idVal) {
          ws->name.set(newName);
          break;
        }
      }
    } else if (evName == "fullscreen") {
      if (HyprlandWorkspace* ws = focusedWorkspace.peek(); ws != nullptr) {
        ws->hasFullscreen.set(evData == "1");
      }
      refreshWorkspaces(false);
    } else if (evName == "openwindow") {
      const auto args = event->parse(4);
      const auto addrOpt = parseAddressOpt(args[0]);
      if (!addrOpt.has_value()) {
        return;
      }
      const std::uint64_t addr = addrOpt.value();
      const std::string& wsName = args[1];
      const std::string& winTitle = args[3];

      auto* ws = findWorkspaceByName(wsName, false);
      if (!ws) {
        return;
      }
      auto* top = findToplevelByAddress(addr, false);
      const bool existed = (top != nullptr);
      if (!top) {
        top = create<HyprlandToplevel>();
      }
      top->updateInitial(addr, winTitle, wsName);
      ws->insertToplevel(top);
      if (!existed) {
        toplevels.peek()->insertObject(top);
      }
    } else if (evName == "closewindow") {
      const auto args = event->parse(1);
      const auto addrOpt = parseAddressOpt(args[0]);
      if (!addrOpt.has_value()) {
        return;
      }
      const auto list = toplevels.peek()->values.peek();
      for (Object* obj : list) {
        auto* top = static_cast<HyprlandToplevel*>(obj);
        if (top->rawAddress() == addrOpt.value()) {
          if (top == activeToplevel.peek()) {
            setActiveToplevel(nullptr);
          }
          toplevels.peek()->removeObject(top);
          if (HyprlandWorkspace* ws = top->workspace.peek(); ws != nullptr) {
            ws->removeToplevel(top);
          }
          top->deleteLater();
          break;
        }
      }
    } else if (evName == "movewindowv2") {
      const auto args = event->parse(3);
      const auto addrOpt = parseAddressOpt(args[0]);
      if (!addrOpt.has_value()) {
        return;
      }
      const std::uint64_t addr = addrOpt.value();
      const std::string& wsName = args[2];

      auto* top = findToplevelByAddress(addr, false);
      if (!top) {
        return;
      }
      auto* ws = findWorkspaceByName(wsName, false);
      if (!ws) {
        return;
      }
      auto* oldWs = top->workspace.peek();
      top->setWorkspace(ws);
      if (oldWs != nullptr) {
        oldWs->removeToplevel(top);
      }
      ws->insertToplevel(top);
    } else if (evName == "windowtitlev2") {
      const auto args = event->parse(2);
      const auto addrOpt = parseAddressOpt(args[0]);
      if (!addrOpt.has_value()) {
        return;
      }
      const std::uint64_t addr = addrOpt.value();
      auto* top = findToplevelByAddress(addr, true);
      if (top != nullptr) {
        top->title.set(args[1]);
      }
    } else if (evName == "activewindowv2") {
      const auto args = event->parse(1);
      const auto addrOpt = parseAddressOpt(args[0]);
      if (!addrOpt.has_value()) {
        return; // Ignore unparsable/empty address, keeping previous activeToplevel (as Quickshell)
      }
      auto* top = findToplevelByAddress(addrOpt.value(), true);
      setActiveToplevel(top);
    } else if (evName == "urgent") {
      const auto args = event->parse(1);
      const auto addrOpt = parseAddressOpt(args[0]);
      if (!addrOpt.has_value()) {
        return;
      }
      const std::uint64_t addr = addrOpt.value();
      auto* top = findToplevelByAddress(addr, true);
      if (top != nullptr) {
        top->urgent.set(true);
      }
    }
  }

  std::vector<std::string> Hyprland::parseEventArgs(std::string_view text, int count) {
    std::vector<std::string> args;
    if (count <= 0) {
      return args;
    }
    args.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count - 1; ++i) {
      const auto splitIdx = text.find(',');
      if (splitIdx == std::string_view::npos) {
        break;
      }
      args.emplace_back(text.substr(0, splitIdx));
      text = text.substr(splitIdx + 1);
    }
    if (!text.empty()) {
      args.emplace_back(text);
    }
    while (static_cast<int>(args.size()) < count) {
      args.emplace_back();
    }
    return args;
  }

} // namespace ii::qs
