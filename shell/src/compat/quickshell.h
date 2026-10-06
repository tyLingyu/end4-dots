#pragma once

// The Quickshell singleton (QuickshellGlobal).

#include "compat/screen.h"
#include "runtime/js.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <string>
#include <vector>

namespace ii::qs {

  class Quickshell : public Object {
  public:
    static Quickshell& instance();

    Property<int> processId;
    // Filled by the program from the Wayland outputs.
    Property<std::vector<ShellScreen*>> screens;
    Property<std::string> shellDir;
    Property<std::string> configDir;
    Property<std::string> shellRoot;
    Property<std::string> workingDirectory;
    Property<bool> watchFiles{true};
    Property<std::string> clipboardText;
    Property<std::string> dataDir;
    Property<std::string> stateDir;
    Property<std::string> cacheDir;
    Signal<> lastWindowClosed;
    Signal<> reloadCompleted;
    Signal<std::string> reloadFailed;

    // A path inside the shell's directory (Quickshell.shellPath).
    [[nodiscard]] std::string shellPath(const std::string& path) const;
    [[nodiscard]] js::Json env(const std::string& name) const;
    // Starts a process that outlives the shell, its output discarded.
    void execDetached(const std::vector<std::string>& command) const;
    void execDetached(const std::string& command) const;

  private:
    Quickshell();
  };

} // namespace ii::qs
