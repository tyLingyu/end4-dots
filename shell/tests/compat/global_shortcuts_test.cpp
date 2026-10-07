#include "compat/global_shortcuts.h"
#include "compat/hyprland.h"
#include "wayland/wayland_connection.h"

#include "../check.h"
#include "../pump.h"

#include <memory>
#include <string>

using namespace ii;

TEST("global_shortcuts: inert without connection and empty name handling") {
  qs::setWaylandConnection(nullptr);
  auto& mgr = qs::GlobalShortcutManager::instance();

  // Empty name shortcut should not register or be pending
  auto emptyShortcut = std::make_unique<qs::GlobalShortcut>();
  emptyShortcut->complete();
  CHECK(!emptyShortcut->isRegistered());
  CHECK(mgr.pendingCount() == 0);

  // Shortcut with name without connection stays inert and pending
  auto s = std::make_unique<qs::GlobalShortcut>();
  s->name.set("inert_test");
  s->complete();
  CHECK(!s->isRegistered());
  CHECK(mgr.pendingCount() == 1);

  // Destroying pending shortcut removes it from pending list
  s.reset();
  CHECK(mgr.pendingCount() == 0);
}

TEST("global_shortcuts: pressed and released signals and property update") {
  auto s = std::make_unique<qs::GlobalShortcut>();
  int pressedCount = 0;
  int releasedCount = 0;
  bool lastPressedProp = false;

  s->pressedSignal.connectForever([&] { ++pressedCount; });
  s->released.connectForever([&] { ++releasedCount; });
  s->pressed.changed().connectForever([&] { lastPressedProp = s->pressed.get(); });

  CHECK(!s->pressed.get());

  s->handlePressed();
  CHECK(s->pressed.get());
  CHECK(lastPressedProp);
  CHECK(pressedCount == 1);
  CHECK(releasedCount == 0);

  s->handleReleased();
  CHECK(!s->pressed.get());
  CHECK(!lastPressedProp);
  CHECK(pressedCount == 1);
  CHECK(releasedCount == 1);
}

TEST("global_shortcuts: dynamic name and appid change updates registration") {
  qs::setWaylandConnection(nullptr);
  auto& mgr = qs::GlobalShortcutManager::instance();

  auto s = std::make_unique<qs::GlobalShortcut>();
  s->name.set("first_name");
  s->complete();
  CHECK(mgr.pendingCount() == 1);

  // Changing name while pending stays pending under new name
  s->name.set("second_name");
  CHECK(mgr.pendingCount() == 1);

  // Clearing name removes from pending
  s->name.set("");
  CHECK(mgr.pendingCount() == 0);

  s->name.set("third_name");
  CHECK(mgr.pendingCount() == 1);

  s.reset();
  CHECK(mgr.pendingCount() == 0);
}

TEST("global_shortcuts: registration, refcounting, and duplicate handling") {
  qs::setWaylandConnection(nullptr);
  auto& mgr = qs::GlobalShortcutManager::instance();

  bool canConnect = false;
  std::unique_ptr<WaylandConnection> wayland;
  try {
    wayland = std::make_unique<WaylandConnection>();
    canConnect = wayland->connect();
  } catch (...) {
    canConnect = false;
  }

  if (!canConnect || wayland == nullptr || !wayland->hasHyprlandGlobalShortcuts()) {
    // If not in a live Hyprland session, skip the live compositor checks
    return;
  }

  // Pre-create shortcut before setting connection to verify deferred registration
  auto s1 = std::make_unique<qs::GlobalShortcut>();
  s1->name.set("unit_test_shortcut");
  s1->description.set("Unit test shortcut");
  s1->complete();
  CHECK(!s1->isRegistered());
  CHECK(mgr.pendingCount() == 1);

  // Set Wayland connection hook; s1 should be registered now
  qs::setWaylandConnection(*wayland);
  CHECK(s1->isRegistered());
  CHECK(mgr.isRegistered("iishell", "unit_test_shortcut"));
  CHECK(mgr.refcount("iishell", "unit_test_shortcut") == 1);
  CHECK(mgr.pendingCount() == 0);

  // Duplicate shortcut with same appid:name should increment refcount (Quickshell parity)
  auto s2 = std::make_unique<qs::GlobalShortcut>();
  s2->name.set("unit_test_shortcut");
  s2->complete();
  CHECK(s2->isRegistered());
  CHECK(mgr.refcount("iishell", "unit_test_shortcut") == 2);

  // Destroying one instance decrements refcount but keeps registration active
  s2.reset();
  CHECK(mgr.isRegistered("iishell", "unit_test_shortcut"));
  CHECK(mgr.refcount("iishell", "unit_test_shortcut") == 1);

  // Changing name re-registers under the new name
  s1->name.set("unit_test_renamed");
  CHECK(!mgr.isRegistered("iishell", "unit_test_shortcut"));
  CHECK(mgr.isRegistered("iishell", "unit_test_renamed"));
  CHECK(mgr.refcount("iishell", "unit_test_renamed") == 1);

  // Destroying last instance unregisters from compositor
  s1.reset();
  CHECK(!mgr.isRegistered("iishell", "unit_test_renamed"));
  CHECK(mgr.activeCount() == 0);

  qs::setWaylandConnection(nullptr);
}

TEST_MAIN()
