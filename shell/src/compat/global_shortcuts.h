#pragma once

#include "runtime/signal.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class WaylandConnection;
struct hyprland_global_shortcut_v1;

namespace ii::qs {

  class GlobalShortcut;

  // Multiplexed representation of a hyprland_global_shortcut_v1 proxy.
  // Multiple GlobalShortcut instances with identical appid + name share one ManagedShortcut.
  // As Quickshell's src/wayland/hyprland/global_shortcuts/{manager,shortcut}.cpp.
  struct ManagedShortcut {
    int refcount = 0;
    hyprland_global_shortcut_v1* wlShortcut = nullptr;
    Signal<> pressed;
    Signal<> released;
  };

  // Internal singleton managing hyprland_global_shortcuts_v1 registrations,
  // matching Quickshell's GlobalShortcutManager (duplicate prevention, refcounting,
  // deferred registration when compositor global is ready).
  class GlobalShortcutManager {
  public:
    static GlobalShortcutManager& instance();

    void registerShortcut(GlobalShortcut* client);
    void unregisterShortcut(GlobalShortcut* client, const std::string& appid, const std::string& name);
    void removePending(GlobalShortcut* client);
    void updateConnection(WaylandConnection* connection);

    [[nodiscard]] bool isRegistered(const std::string& appid, const std::string& name) const;
    [[nodiscard]] int refcount(const std::string& appid, const std::string& name) const;
    [[nodiscard]] std::size_t activeCount() const;
    [[nodiscard]] std::size_t pendingCount() const;

  private:
    GlobalShortcutManager() = default;
    ~GlobalShortcutManager();

    GlobalShortcutManager(const GlobalShortcutManager&) = delete;
    GlobalShortcutManager& operator=(const GlobalShortcutManager&) = delete;

    WaylandConnection* m_wayland = nullptr;
    std::vector<GlobalShortcut*> m_pendingShortcuts;
    std::unordered_map<std::string, std::unique_ptr<ManagedShortcut>> m_shortcuts;
    bool m_loggedNoManager = false;
    bool m_loggedNoConnection = false;
  };

} // namespace ii::qs
