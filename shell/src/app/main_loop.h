#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <vector>

class PollSource;
class WaylandConnection;

class MainLoop {
public:
  using PollSourcesProvider = std::function<std::vector<PollSource*>()>;
  using ShutdownCallback = std::function<void()>;

  // ii-shell: Noctalia's loop took its Bar to close surfaces on exit; that is now onShutdown.
  MainLoop(WaylandConnection& wayland, PollSourcesProvider sourcesProvider, ShutdownCallback onShutdown = {});

  void run();

  // Safe to call from signal handlers.
  static void requestShutdown() noexcept { s_shutdownRequested = true; }

private:
  static std::atomic<bool> s_shutdownRequested;

  WaylandConnection& m_wayland;
  PollSourcesProvider m_sourcesProvider;
  ShutdownCallback m_onShutdown;

  // Absolute deadline by which each source must next be dispatched, armed from
  // the timeout it advertised and retained across iterations until it fires.
  std::unordered_map<PollSource*, std::chrono::steady_clock::time_point> m_sourceDeadlines;
};
