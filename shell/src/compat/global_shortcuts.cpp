#include "compat/global_shortcuts.h"

#include "compat/hyprland.h"
#include "core/log.h"
#include "hyprland-global-shortcuts-v1-client-protocol.h"
#include "wayland/wayland_connection.h"

#include <algorithm>
#include <utility>
#include <wayland-client.h>

namespace ii::qs {

  namespace {
    constexpr Logger kLog("shortcuts");

    // as Quickshell's src/wayland/hyprland/global_shortcuts/shortcut.cpp
    const hyprland_global_shortcut_v1_listener kShortcutListener = {
        .pressed = [](void* data, hyprland_global_shortcut_v1* /*shortcut*/,
                      uint32_t /*tv_sec_hi*/, uint32_t /*tv_sec_lo*/, uint32_t /*tv_nsec*/) {
          auto* managed = static_cast<ManagedShortcut*>(data);
          if (managed != nullptr) {
            managed->pressed.emit();
          }
        },
        .released = [](void* data, hyprland_global_shortcut_v1* /*shortcut*/,
                       uint32_t /*tv_sec_hi*/, uint32_t /*tv_sec_lo*/, uint32_t /*tv_nsec*/) {
          auto* managed = static_cast<ManagedShortcut*>(data);
          if (managed != nullptr) {
            managed->released.emit();
          }
        },
    };

    WaylandConnection* g_waylandConnection = nullptr;
  } // namespace

  void setWaylandConnection(WaylandConnection* connection) {
    g_waylandConnection = connection;
    GlobalShortcutManager::instance().updateConnection(connection);
  }

  WaylandConnection* waylandConnection() {
    return g_waylandConnection;
  }

  // ── GlobalShortcutManager ──────────────────────────────────────────────────
  // as Quickshell's src/wayland/hyprland/global_shortcuts/manager.cpp

  GlobalShortcutManager& GlobalShortcutManager::instance() {
    static GlobalShortcutManager instance;
    return instance;
  }

  GlobalShortcutManager::~GlobalShortcutManager() {
    updateConnection(nullptr);
  }

  void GlobalShortcutManager::registerShortcut(GlobalShortcut* client) {
    if (client == nullptr) {
      return;
    }
    const std::string nameVal = client->name.peek();
    if (nameVal.empty()) {
      return;
    }
    const std::string appidVal = client->appid.peek();
    const std::string key = appidVal + ":" + nameVal;

    if (m_wayland == nullptr) {
      if (!m_loggedNoConnection) {
        m_loggedNoConnection = true;
        kLog.warn("Wayland connection not set; hyprland_global_shortcuts_v1 is unavailable. GlobalShortcut will stay inert until connected.");
      }
      if (std::find(m_pendingShortcuts.begin(), m_pendingShortcuts.end(), client) == m_pendingShortcuts.end()) {
        m_pendingShortcuts.push_back(client);
      }
      return;
    }

    auto* manager = m_wayland->hyprlandGlobalShortcutsManager();
    if (manager == nullptr) {
      if (!m_loggedNoManager) {
        m_loggedNoManager = true;
        kLog.warn("The active compositor does not support hyprland_global_shortcuts_v1. GlobalShortcut will not work.");
      }
      if (std::find(m_pendingShortcuts.begin(), m_pendingShortcuts.end(), client) == m_pendingShortcuts.end()) {
        m_pendingShortcuts.push_back(client);
      }
      return;
    }

    std::erase(m_pendingShortcuts, client);

    auto it = m_shortcuts.find(key);
    if (it != m_shortcuts.end() && it->second != nullptr) {
      // as Quickshell's manager.cpp duplicate handling: increment refcount, reuse shortcut proxy
      it->second->refcount++;
      client->bindTo(it->second.get(), appidVal, nameVal);
      return;
    }

    const std::string desc = client->description.peek();
    const std::string triggerDesc = client->triggerDescription.peek();

    // hyprland_global_shortcuts_manager_v1_register_shortcut takes:
    // (manager, id [name], app_id, description, trigger_description)
    auto* wlShortcut = hyprland_global_shortcuts_manager_v1_register_shortcut(
        manager,
        nameVal.c_str(),
        appidVal.c_str(),
        desc.c_str(),
        triggerDesc.c_str()
    );

    if (wlShortcut == nullptr) {
      kLog.error("failed to register global shortcut {}:{}", appidVal, nameVal);
      return;
    }

    if (m_wayland->display() != nullptr) {
      wl_display_flush(m_wayland->display());
    }

    auto managed = std::make_unique<ManagedShortcut>();
    managed->refcount = 1;
    managed->wlShortcut = wlShortcut;

    hyprland_global_shortcut_v1_add_listener(wlShortcut, &kShortcutListener, managed.get());

    client->bindTo(managed.get(), appidVal, nameVal);
    m_shortcuts[key] = std::move(managed);
  }

  void GlobalShortcutManager::unregisterShortcut(GlobalShortcut* client, const std::string& appid, const std::string& name) {
    removePending(client);

    const std::string key = appid + ":" + name;
    auto it = m_shortcuts.find(key);
    if (it == m_shortcuts.end() || it->second == nullptr) {
      return;
    }

    // as Quickshell's manager.cpp: unregisterShortcut
    if (it->second->refcount > 1) {
      it->second->refcount--;
    } else {
      if (it->second->wlShortcut != nullptr) {
        hyprland_global_shortcut_v1_destroy(it->second->wlShortcut);
        it->second->wlShortcut = nullptr;
        if (m_wayland != nullptr && m_wayland->display() != nullptr) {
          wl_display_flush(m_wayland->display());
        }
      }
      m_shortcuts.erase(it);
    }
  }

  void GlobalShortcutManager::removePending(GlobalShortcut* client) {
    std::erase(m_pendingShortcuts, client);
  }

  void GlobalShortcutManager::updateConnection(WaylandConnection* connection) {
    m_wayland = connection;
    if (m_wayland == nullptr) {
      for (auto& [key, managed] : m_shortcuts) {
        if (managed != nullptr && managed->wlShortcut != nullptr) {
          hyprland_global_shortcut_v1_destroy(managed->wlShortcut);
          managed->wlShortcut = nullptr;
        }
      }
      m_shortcuts.clear();
      m_pendingShortcuts.clear();
      return;
    }

    auto* manager = m_wayland->hyprlandGlobalShortcutsManager();
    if (manager == nullptr) {
      if (!m_loggedNoManager) {
        m_loggedNoManager = true;
        kLog.warn("The active compositor does not support hyprland_global_shortcuts_v1. GlobalShortcut will not work.");
      }
      return;
    }

    auto pending = std::move(m_pendingShortcuts);
    m_pendingShortcuts.clear();
    for (auto* client : pending) {
      if (client != nullptr && client->isCompleted() && !client->name.peek().empty() && !client->isRegistered()) {
        registerShortcut(client);
      }
    }
  }

  bool GlobalShortcutManager::isRegistered(const std::string& appid, const std::string& name) const {
    const std::string key = appid + ":" + name;
    auto it = m_shortcuts.find(key);
    return it != m_shortcuts.end() && it->second != nullptr;
  }

  int GlobalShortcutManager::refcount(const std::string& appid, const std::string& name) const {
    const std::string key = appid + ":" + name;
    auto it = m_shortcuts.find(key);
    if (it != m_shortcuts.end() && it->second != nullptr) {
      return it->second->refcount;
    }
    return 0;
  }

  std::size_t GlobalShortcutManager::activeCount() const {
    return m_shortcuts.size();
  }

  std::size_t GlobalShortcutManager::pendingCount() const {
    return m_pendingShortcuts.size();
  }

  // ── GlobalShortcut ─────────────────────────────────────────────────────────
  // as Quickshell's src/wayland/hyprland/global_shortcuts/qml.cpp

  GlobalShortcut::GlobalShortcut() {
    appid.changed().connectForever([this] {
      if (isCompleted()) {
        updateRegistration();
      }
    });
    name.changed().connectForever([this] {
      if (isCompleted()) {
        updateRegistration();
      }
    });
  }

  GlobalShortcut::~GlobalShortcut() {
    if (m_isRegistered) {
      const std::string oldAppid = m_registeredAppid;
      const std::string oldName = m_registeredName;
      unbind();
      GlobalShortcutManager::instance().unregisterShortcut(this, oldAppid, oldName);
    } else {
      GlobalShortcutManager::instance().removePending(this);
    }
  }

  void GlobalShortcut::componentComplete() {
    updateRegistration();
  }

  void GlobalShortcut::updateRegistration() {
    const std::string newAppid = appid.peek();
    const std::string newName = name.peek();

    if (m_isRegistered && m_registeredAppid == newAppid && m_registeredName == newName) {
      return;
    }

    if (m_isRegistered) {
      const std::string oldAppid = m_registeredAppid;
      const std::string oldName = m_registeredName;
      unbind();
      GlobalShortcutManager::instance().unregisterShortcut(this, oldAppid, oldName);
    } else {
      GlobalShortcutManager::instance().removePending(this);
    }

    if (newName.empty()) {
      kLog.warn("Unable to create GlobalShortcut with empty name.");
      return;
    }

    GlobalShortcutManager::instance().registerShortcut(this);
  }

  void GlobalShortcut::handlePressed() {
    pressed.writeDirect(true);
    pressedSignal.emit();
  }

  void GlobalShortcut::handleReleased() {
    pressed.writeDirect(false);
    released.emit();
  }

  void GlobalShortcut::bindTo(ManagedShortcut* managed, std::string appidVal, std::string nameVal) {
    m_registeredAppid = std::move(appidVal);
    m_registeredName = std::move(nameVal);
    m_isRegistered = true;
    m_pressedConn = managed->pressed.connect([this] { handlePressed(); });
    m_releasedConn = managed->released.connect([this] { handleReleased(); });
  }

  void GlobalShortcut::unbind() {
    m_pressedConn.disconnect();
    m_releasedConn.disconnect();
    m_isRegistered = false;
    m_registeredAppid.clear();
    m_registeredName.clear();
  }

} // namespace ii::qs
