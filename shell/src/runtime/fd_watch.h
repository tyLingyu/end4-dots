#pragma once

#include <cstdint>
#include <functional>

class PollSource;

namespace ii {

  // File descriptors the main loop watches for the runtime and compat layer (processes, sockets,
  // PipeWire, D-Bus, inotify), all through one PollSource the program hands to its MainLoop.
  class FdWatch {
  public:
    using Callback = std::function<void(short revents)>;
    using Id = std::uint64_t;

    // Calls `callback` from the main loop whenever `fd` has any of `events` (or an error/hangup).
    static Id watch(int fd, short events, Callback callback);
    // Safe to call from inside any callback, including the watch's own.
    static void unwatch(Id id);
    static void setEvents(Id id, short events);

    [[nodiscard]] static PollSource& pollSource();
  };

} // namespace ii
