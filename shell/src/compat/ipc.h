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

  private:
    std::map<std::string, Function> m_functions;
  };

} // namespace ii::qs
