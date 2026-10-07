// Tests for Quickshell Hyprland IPC compatibility and regression checks.

#include "compat/hyprland.h"

#include "../check.h"
#include "../pump.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

using namespace ii;

namespace {

  class FakeHyprland {
  public:
    std::filesystem::path dir;
    int reqListenFd = -1;
    int eventListenFd = -1;
    int eventClientFd = -1;

    FdWatch::Id reqListenWatch = 0;
    FdWatch::Id eventListenWatch = 0;
    FdWatch::Id eventClientWatch = 0;

    std::vector<std::string> receivedDispatches;
    int monitorsReqCount = 0;
    bool closeNextReq = false;

    FakeHyprland() {
      dir = std::filesystem::temp_directory_path() / ("ii-fake-hypr-" + std::to_string(::getpid()));
      std::filesystem::remove_all(dir);
      std::filesystem::create_directories(dir / "hypr" / "fake-sig");

      ::setenv("XDG_RUNTIME_DIR", dir.c_str(), 1);
      ::setenv("HYPRLAND_INSTANCE_SIGNATURE", "fake-sig", 1);

      const auto reqPath = dir / "hypr" / "fake-sig" / ".socket.sock";
      const auto eventPath = dir / "hypr" / "fake-sig" / ".socket2.sock";

      reqListenFd = bindListen(reqPath);
      eventListenFd = bindListen(eventPath);

      reqListenWatch = FdWatch::watch(reqListenFd, POLLIN, [this](short) {
        onReqAccept();
      });

      eventListenWatch = FdWatch::watch(eventListenFd, POLLIN, [this](short) {
        onEventAccept();
      });
    }

    ~FakeHyprland() {
      if (reqListenWatch != 0) FdWatch::unwatch(reqListenWatch);
      if (eventListenWatch != 0) FdWatch::unwatch(eventListenWatch);
      if (eventClientWatch != 0) FdWatch::unwatch(eventClientWatch);
      if (reqListenFd >= 0) ::close(reqListenFd);
      if (eventListenFd >= 0) ::close(eventListenFd);
      if (eventClientFd >= 0) ::close(eventClientFd);
      std::filesystem::remove_all(dir);
    }

    static int bindListen(const std::filesystem::path& path) {
      const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
      sockaddr_un addr{};
      addr.sun_family = AF_UNIX;
      std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
      ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
      ::listen(fd, 10);
      return fd;
    }

    void onEventAccept() {
      const int client = ::accept4(eventListenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
      if (client >= 0) {
        if (eventClientFd >= 0) {
          if (eventClientWatch != 0) FdWatch::unwatch(eventClientWatch);
          ::close(eventClientFd);
        }
        eventClientFd = client;
        eventClientWatch = FdWatch::watch(eventClientFd, POLLIN, [this](short revents) {
          if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            if (eventClientWatch != 0) {
              FdWatch::unwatch(eventClientWatch);
              eventClientWatch = 0;
            }
            ::close(eventClientFd);
            eventClientFd = -1;
          }
        });
      }
    }

    void onReqAccept() {
      const int client = ::accept4(reqListenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
      if (client < 0) return;

      if (closeNextReq) {
        closeNextReq = false;
        ::close(client);
        return;
      }

      auto reqBuf = std::make_shared<std::string>();
      auto watchId = std::make_shared<FdWatch::Id>(0);
      *watchId = FdWatch::watch(client, POLLIN, [this, client, reqBuf, watchId](short revents) {
        if ((revents & POLLIN) != 0) {
          char buf[1024];
          const ssize_t n = ::read(client, buf, sizeof(buf));
          if (n > 0) {
            reqBuf->append(buf, static_cast<std::size_t>(n));
            handleReq(client, *reqBuf, *watchId);
          } else {
            FdWatch::unwatch(*watchId);
            ::close(client);
          }
        } else {
          FdWatch::unwatch(*watchId);
          ::close(client);
        }
      });
    }

    void handleReq(int client, const std::string& req, FdWatch::Id watchId) {
      std::string resp;
      if (req == "j/monitors") {
        ++monitorsReqCount;
        resp = R"([
          {
            "id": 0,
            "name": "DP-1",
            "description": "Test Display 1",
            "x": 0,
            "y": 0,
            "width": 1920,
            "height": 1080,
            "scale": 1.0,
            "focused": true,
            "activeWorkspace": {
              "id": 1,
              "name": "1"
            }
          },
          {
            "id": 1,
            "name": "HDMI-A-1",
            "description": "Test Display 2",
            "x": 1920,
            "y": 0,
            "width": 2560,
            "height": 1440,
            "scale": 1.25,
            "focused": false,
            "activeWorkspace": {
              "id": 2,
              "name": "2"
            }
          }
        ])";
      } else if (req == "j/workspaces") {
        resp = R"([
          {
            "id": 1,
            "name": "1",
            "monitor": "DP-1",
            "monitorID": 0,
            "hasfullscreen": false
          },
          {
            "id": 2,
            "name": "2",
            "monitor": "HDMI-A-1",
            "monitorID": 1,
            "hasfullscreen": true
          }
        ])";
      } else if (req == "j/clients") {
        resp = R"([
          {
            "address": "0x55aa11223344",
            "title": "Terminal",
            "workspace": {
              "id": 1,
              "name": "1"
            }
          }
        ])";
      } else if (req.starts_with("dispatch ")) {
        receivedDispatches.push_back(req);
        resp = "ok";
      } else {
        resp = "ok";
      }

      FdWatch::unwatch(watchId);
      const ssize_t written = ::write(client, resp.data(), resp.size());
      (void)written;
      ::close(client);
    }

    void sendEvent(const std::string& ev) {
      if (eventClientFd >= 0) {
        const ssize_t written = ::write(eventClientFd, ev.data(), ev.size());
        (void)written;
      }
    }
  };

  FakeHyprland& fakeServer() {
    static FakeHyprland instance;
    return instance;
  }

} // namespace

TEST("hyprland: fake hyprland initialization, models, events, dispatch") {
  FakeHyprland& fake = fakeServer();

  // Hyprland::instance() is initialized after environment variables are set by FakeHyprland
  auto& hypr = qs::Hyprland::instance();

  // 1. Initial models loaded via j/monitors, j/workspaces, j/clients
  CHECK(ii_test::pumpUntil([&] {
    return hypr.monitors.peek()->values.peek().size() == 2 &&
           hypr.workspaces.peek()->values.peek().size() == 2 &&
           hypr.toplevels.peek()->values.peek().size() == 1;
  }));

  // Socket paths
  CHECK(hypr.requestSocketPath.peek().ends_with("/.socket.sock"));
  CHECK(hypr.eventSocketPath.peek().ends_with("/.socket2.sock"));

  // Check monitor contents
  auto* m0 = static_cast<qs::HyprlandMonitor*>(hypr.monitors.peek()->values.peek()[0]);
  auto* m1 = static_cast<qs::HyprlandMonitor*>(hypr.monitors.peek()->values.peek()[1]);
  CHECK(m0->name.peek() == "DP-1");
  CHECK(m0->width.peek() == 1920);
  CHECK(m0->focused.peek() == true);
  CHECK(m1->name.peek() == "HDMI-A-1");
  CHECK(m1->width.peek() == 2560);
  CHECK(m1->scale.peek() == 1.25);
  CHECK(m1->focused.peek() == false);
  CHECK(hypr.focusedMonitor.peek() == m0);

  // Check workspace contents
  auto* w0 = static_cast<qs::HyprlandWorkspace*>(hypr.workspaces.peek()->values.peek()[0]);
  auto* w1 = static_cast<qs::HyprlandWorkspace*>(hypr.workspaces.peek()->values.peek()[1]);
  CHECK(w0->id.peek() == 1);
  CHECK(w0->name.peek() == "1");
  CHECK(w0->active.peek() == true);
  CHECK(w0->focused.peek() == true);
  CHECK(w1->id.peek() == 2);
  CHECK(w1->name.peek() == "2");
  CHECK(w1->hasFullscreen.peek() == true);
  CHECK(w1->focused.peek() == false);
  CHECK(hypr.focusedWorkspace.peek() == w0);

  // Check toplevel contents
  auto* top0 = static_cast<qs::HyprlandToplevel*>(hypr.toplevels.peek()->values.peek()[0]);
  CHECK(top0->address.peek() == "55aa11223344");
  CHECK(top0->title.peek() == "Terminal");
  CHECK(top0->workspace.peek() == w0);
  CHECK(top0->monitor.peek() == m0);

  // 2. focusedmon event
  fake.sendEvent("focusedmon>>HDMI-A-1,2\n");
  CHECK(ii_test::pumpUntil([&] {
    return hypr.focusedMonitor.peek() != nullptr &&
           hypr.focusedMonitor.peek()->name.peek() == "HDMI-A-1";
  }));
  CHECK(m0->focused.peek() == false);
  CHECK(m1->focused.peek() == true);
  CHECK(w0->focused.peek() == false);
  CHECK(w1->focused.peek() == true);
  CHECK(hypr.focusedWorkspace.peek() == w1);

  // 3. rawEvent delivery and parse
  std::string lastEventName;
  std::vector<std::string> lastParsed;
  const Connection rawConn = hypr.rawEvent.connect([&](qs::HyprlandEvent* ev) {
    lastEventName = ev->name.peek();
    lastParsed = ev->parse(2);
  });
  fake.sendEvent("customtest>>hello,world\n");
  CHECK(ii_test::pumpUntil([&] { return lastEventName == "customtest"; }));
  CHECK(lastParsed.size() == 2);
  CHECK(lastParsed[0] == "hello");
  CHECK(lastParsed[1] == "world");

  // 4. dispatch wire format
  hypr.dispatch("workspace 3");
  CHECK(ii_test::pumpUntil([&] { return !fake.receivedDispatches.empty(); }));
  CHECK(fake.receivedDispatches.back() == "dispatch workspace 3");

  // Also test workspace::activate dispatch
  w0->activate();
  CHECK(ii_test::pumpUntil([&] { return fake.receivedDispatches.size() >= 2; }));
  CHECK(fake.receivedDispatches.back() == "dispatch workspace 1");

  // 5. Workspace create / destroy
  fake.sendEvent("createworkspacev2>>3,3\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.workspaces.peek()->values.peek().size() == 3; }));
  auto* w2 = static_cast<qs::HyprlandWorkspace*>(hypr.workspaces.peek()->values.peek()[2]);
  CHECK(w2->id.peek() == 3);
  CHECK(w2->name.peek() == "3");

  fake.sendEvent("destroyworkspacev2>>3,3\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.workspaces.peek()->values.peek().size() == 2; }));

  // 6. Window open / close
  fake.sendEvent("openwindow>>0x55aa99887766,1,myclass,New Window\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.toplevels.peek()->values.peek().size() == 2; }));
  auto* top1 = static_cast<qs::HyprlandToplevel*>(hypr.toplevels.peek()->values.peek()[1]);
  CHECK(top1->title.peek() == "New Window");
  CHECK(top1->address.peek() == "55aa99887766");

  fake.sendEvent("closewindow>>0x55aa99887766\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.toplevels.peek()->values.peek().size() == 1; }));
}

TEST("hyprland regression: monitor removal nulls workspace monitor and focusedMonitor (no UAF)") {
  FakeHyprland& fake = fakeServer();
  auto& hypr = qs::Hyprland::instance();

  // Add temporary monitor
  fake.sendEvent("monitoraddedv2>>10,TEMP-MON,Temp Display\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.monitors.peek()->values.peek().size() == 3; }));

  auto* tempMon = static_cast<qs::HyprlandMonitor*>(hypr.monitors.peek()->values.peek()[2]);
  CHECK(tempMon->name.peek() == "TEMP-MON");

  auto* w0 = static_cast<qs::HyprlandWorkspace*>(hypr.workspaces.peek()->values.peek()[0]);
  w0->setMonitor(tempMon);
  CHECK(w0->monitor.peek() == tempMon);

  hypr.setFocusedMonitor(tempMon);
  CHECK(hypr.focusedMonitor.peek() == tempMon);

  // Remove the monitor
  fake.sendEvent("monitorremoved>>TEMP-MON\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.monitors.peek()->values.peek().size() == 2; }));
  ii_test::drainDeferred();

  // Monitor was deferred-deleted; ensure back-pointers are nulled and safe
  CHECK(w0->monitor.peek() == nullptr);
  CHECK(hypr.focusedMonitor.peek() != tempMon);

  // Accessing workspace properties and refreshing does not cause UAF
  CHECK(w0->active.peek() == false);
  fake.sendEvent("fullscreen>>1\n");
  ii_test::pumpFor(50);
}

TEST("hyprland regression: workspace removal nulls monitor activeWorkspace and toplevel workspace (no UAF)") {
  FakeHyprland& fake = fakeServer();
  auto& hypr = qs::Hyprland::instance();

  fake.sendEvent("createworkspacev2>>4,4\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.workspaces.peek()->values.peek().size() == 3; }));

  auto* w4 = static_cast<qs::HyprlandWorkspace*>(hypr.workspaces.peek()->values.peek()[2]);
  CHECK(w4->id.peek() == 4);

  fake.sendEvent("openwindow>>0x11223344,4,test,Test Window\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.toplevels.peek()->values.peek().size() == 2; }));

  auto* top = static_cast<qs::HyprlandToplevel*>(hypr.toplevels.peek()->values.peek()[1]);
  CHECK(top->workspace.peek() == w4);

  auto* m1 = static_cast<qs::HyprlandMonitor*>(hypr.monitors.peek()->values.peek()[1]);
  m1->setActiveWorkspace(w4);
  CHECK(m1->activeWorkspace.peek() == w4);

  // Destroy workspace 4
  fake.sendEvent("destroyworkspacev2>>4,4\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.workspaces.peek()->values.peek().size() == 2; }));
  ii_test::drainDeferred();

  // Workspace was deleted; verify back-pointers are nulled
  CHECK(m1->activeWorkspace.peek() == nullptr);
  CHECK(top->workspace.peek() == nullptr);

  // Close window afterwards without UAF
  fake.sendEvent("closewindow>>0x11223344\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.toplevels.peek()->values.peek().size() == 1; }));
  ii_test::drainDeferred();
}

TEST("hyprland regression: toplevel removal cleans up from all workspaces and activeToplevel (no UAF)") {
  FakeHyprland& fake = fakeServer();
  auto& hypr = qs::Hyprland::instance();

  fake.sendEvent("openwindow>>0x88776655,1,test,Urgent Win\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.toplevels.peek()->values.peek().size() == 2; }));

  auto* top = static_cast<qs::HyprlandToplevel*>(hypr.toplevels.peek()->values.peek()[1]);
  auto* w0 = static_cast<qs::HyprlandWorkspace*>(hypr.workspaces.peek()->values.peek()[0]);
  auto* w1 = static_cast<qs::HyprlandWorkspace*>(hypr.workspaces.peek()->values.peek()[1]);

  // Insert toplevel into second workspace as well
  w1->insertToplevel(top);
  CHECK(w1->toplevels.peek()->indexOf(top) != -1);

  // Set urgent and active
  fake.sendEvent("urgent>>0x88776655\n");
  fake.sendEvent("activewindowv2>>0x88776655\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.activeToplevel.peek() == top; }));

  // Close window
  fake.sendEvent("closewindow>>0x88776655\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.toplevels.peek()->values.peek().size() == 1; }));
  ii_test::drainDeferred();

  // Verify removed from activeToplevel and from all workspaces
  CHECK(hypr.activeToplevel.peek() == nullptr);
  CHECK(w0->toplevels.peek()->indexOf(top) == -1);
  CHECK(w1->toplevels.peek()->indexOf(top) == -1);

  w0->updateUrgent();
  w1->updateUrgent();
}

TEST("hyprland regression: destroyworkspacev2 re-queries monitors if activeWorkspace becomes null") {
  FakeHyprland& fake = fakeServer();
  auto& hypr = qs::Hyprland::instance();

  fake.sendEvent("createworkspacev2>>5,5\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.workspaces.peek()->values.peek().size() == 3; }));

  auto* w5 = static_cast<qs::HyprlandWorkspace*>(hypr.workspaces.peek()->values.peek()[2]);
  auto* m0 = static_cast<qs::HyprlandMonitor*>(hypr.monitors.peek()->values.peek()[0]);
  m0->setActiveWorkspace(w5);
  CHECK(m0->activeWorkspace.peek() == w5);

  const int beforeReqs = fake.monitorsReqCount;
  fake.sendEvent("destroyworkspacev2>>5,5\n");
  CHECK(ii_test::pumpUntil([&] { return hypr.workspaces.peek()->values.peek().size() == 2; }));

  // Should have triggered refreshMonitors(false) because m0's activeWorkspace became null
  CHECK(ii_test::pumpUntil([&] { return fake.monitorsReqCount > beforeReqs; }));
  ii_test::drainDeferred();
}

TEST("hyprland regression: refreshMonitors prunes stale/placeholder monitors unconditionally") {
  auto& hypr = qs::Hyprland::instance();
  // Preemptively create a placeholder monitor
  auto* ghost = hypr.findMonitorByName("GHOST-1", true);
  CHECK(ghost != nullptr);
  CHECK(hypr.monitors.peek()->values.peek().size() == 3);

  // Call refreshMonitors(false): canCreate is false, but stale monitors MUST be pruned
  hypr.refreshMonitors(false);
  CHECK(ii_test::pumpUntil([&] { return hypr.monitors.peek()->values.peek().size() == 2; }));
  ii_test::drainDeferred();

  for (Object* obj : hypr.monitors.peek()->values.peek()) {
    auto* m = static_cast<qs::HyprlandMonitor*>(obj);
    CHECK(m->name.peek() != "GHOST-1");
  }
}

TEST("hyprland regression: activewindowv2 with empty or unparsable address is ignored") {
  FakeHyprland& fake = fakeServer();
  auto& hypr = qs::Hyprland::instance();

  auto* top0 = static_cast<qs::HyprlandToplevel*>(hypr.toplevels.peek()->values.peek()[0]);
  hypr.setActiveToplevel(top0);
  CHECK(hypr.activeToplevel.peek() == top0);

  // Send empty activewindowv2 event
  fake.sendEvent("activewindowv2>>\n");
  ii_test::pumpFor(50);
  CHECK(hypr.activeToplevel.peek() == top0);

  // Send unparsable address
  fake.sendEvent("activewindowv2>>invalid_hex_string\n");
  ii_test::pumpFor(50);
  CHECK(hypr.activeToplevel.peek() == top0);
}

TEST("hyprland regression: request writing handles closed socket without SIGPIPE crash") {
  FakeHyprland& fake = fakeServer();
  auto& hypr = qs::Hyprland::instance();

  // Close the accepted socket immediately on next request
  fake.closeNextReq = true;
  hypr.dispatch("workspace 99");

  // Should complete safely without SIGPIPE crashing the process
  ii_test::pumpFor(50);
}

TEST_MAIN()
