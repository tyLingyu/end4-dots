#pragma once

// Quickshell IpcHandler: functions `ii-shell ipc call <target> <function> [args]` can run. The
// handlers register here; the socket server that dispatches calls is in compat/ipc.cpp.

#include "runtime/object.h"
#include "runtime/property.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ii::qs {

  class IpcHandler : public Object {
  public:
    using Function = std::function<std::string(const std::vector<std::string>& args)>;

    IpcHandler();
    ~IpcHandler() override;

    Property<bool> enabled{true};
    Property<std::string> target;

    void addFunction(std::string name, Function function);

    // The handler for `target` (enabled), if any: how the server finds a call's function.
    [[nodiscard]] static const Function* find(const std::string& target, const std::string& function);

    struct TargetInfo {
      std::string name;
      std::vector<std::string> functions;
    };

    [[nodiscard]] static bool hasTarget(const std::string& target);
    [[nodiscard]] static std::vector<TargetInfo> activeTargets();
    [[nodiscard]] static std::vector<std::string> targetFunctions(const std::string& target);

  private:
    std::map<std::string, Function> m_functions;
  };

  // Unix socket IPC server matching Quickshell revision 7511545 semantics
  // (/tmp/qs/q/src/ipc/ipc.cpp and /tmp/qs/q/src/io/ipccomm.cpp).
  class IpcServer {
  public:
    // Starts the unix domain socket server on FdWatch.
    // If socketPath is empty, defaults to $XDG_RUNTIME_DIR/ii-shell/ipc.sock.
    static bool start(const std::string& socketPath = "");
    static void stop();
    [[nodiscard]] static bool isRunning();
    [[nodiscard]] static std::string defaultSocketPath();
  };

  class IpcClient {
  public:
    enum class Status {
      Ok,
      TargetNotFound,
      FunctionNotFound,
      Error,
      ConnectionFailed,
    };

    struct Result {
      Status status{Status::Error};
      std::string value;
    };

    static Result call(const std::string& target, const std::string& function,
                       const std::vector<std::string>& args = {},
                       const std::string& socketPath = "");

    static Result show(const std::string& target = "",
                       const std::string& function = "",
                       const std::string& socketPath = "");
  };

  // CLI entry point: handles `ii-shell ipc call <target> <function> [args...]`
  // and `ii-shell ipc show [target] [function]`. Returns exit code (0 on success, non-zero on error).
  int handleIpcCli(int argc, char** argv);

} // namespace ii::qs
