#include "compat/quickshell.h"

#include "compat/process.h"
#include "runtime/qt.h"

#include <cstdlib>
#include <unistd.h>

namespace ii::qs {

  Quickshell& Quickshell::instance() {
    static Quickshell* self = [] {
      auto* q = new Quickshell();
      q->complete();
      return q;
    }();
    return *self;
  }

  Quickshell::Quickshell() {
    processId.set(static_cast<int>(::getpid()));
    shellDir.set(ii::shellRoot());
    shellRoot.set(ii::shellRoot());
    configDir.set(ii::shellRoot());
  }

  std::string Quickshell::shellPath(const std::string& path) const {
    return ii::shellRoot() + (path.empty() || path.front() == '/' ? "" : "/") + path;
  }

  js::Json Quickshell::env(const std::string& name) const {
    const char* value = std::getenv(name.c_str());
    return value != nullptr ? js::Json(value) : js::Json(nullptr);
  }

  void Quickshell::execDetached(const std::vector<std::string>& command) const {
    Process process;
    process.command.set(command);
    process.startDetached();
  }

  void Quickshell::execDetached(const std::string& command) const { execDetached(std::vector<std::string>{command}); }

} // namespace ii::qs
