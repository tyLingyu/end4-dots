// The generated OSD (tools/qml2cpp output in src/ii) running headless on the runtime: loading
// and unloading the panel with GlobalStates.osdVolumeOpen, the indicator chosen by URL, the
// config written with Quickshell's JSON. XDG dirs point at a scratch directory, so the user's
// config is never read or written.

#include "core/deferred_call.h"
#include "ii/GlobalStates.h"
#include "ii/components.h"
#include "ii/modules/common/Config.h"
#include "ii/modules/ii/onScreenDisplay/OnScreenDisplay.h"
#include "runtime/loader.h"

#include "../check.h"
#include "../pump.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace ii;

using ii_test::drainDeferred;

namespace {

  const std::filesystem::path& scratch() {
    static const std::filesystem::path dir = [] {
      auto path = std::filesystem::temp_directory_path() / ("ii-osd-test-" + std::to_string(::getpid()));
      // The config directory exists, as on every start but the first: Config only becomes ready
      // if its watcher finds the directory (see "Config" in COMPAT.md's upstream bugs).
      std::filesystem::create_directories(path / "config" / "illogical-impulse");
      ::setenv("XDG_CONFIG_HOME", (path / "config").c_str(), 1);
      ::setenv("XDG_STATE_HOME", (path / "state").c_str(), 1);
      ::setenv("XDG_CACHE_HOME", (path / "cache").c_str(), 1);
      return path;
    }();
    return dir;
  }

} // namespace

TEST("osd: the panel loads with osdVolumeOpen, the indicator by URL") {
  (void)scratch();
  Loader::setResolver(generatedComponent);
  auto osd = std::make_unique<onScreenDisplay::OnScreenDisplay>();
  osd->complete();
  drainDeferred();

  auto& states = GlobalStates::instance();
  CHECK(!states.osdVolumeOpen.peek());
  CHECK(osd->osdLoader->item.peek() == nullptr);

  states.osdVolumeOpen.set(true);
  auto* window = dynamic_cast<qs::PanelWindow*>(osd->osdLoader->item.peek());
  CHECK(window != nullptr);
  if (window != nullptr) {
    CHECK(window->layershell.namespace_.peek() == "quickshell:onScreenDisplay");
    CHECK(window->layershell.layer.peek() == qs::WlrLayer::Overlay);
    CHECK(window->exclusionMode.peek() == qs::ExclusionMode::Ignore);
  }
  // currentIndicator is "volume": the indicator loader found VolumeIndicator.qml's component.
  bool foundIndicator = false;
  std::function<void(Item*)> walk = [&](Item* item) {
    if (auto* loader = dynamic_cast<Loader*>(item); loader != nullptr && loader->item.peek() != nullptr) {
      foundIndicator = foundIndicator || loader->source.peek() == "indicators/VolumeIndicator.qml";
    }
    for (Item* child : item->childItems()) {
      walk(child);
    }
  };
  if (window != nullptr) {
    walk(window->contentItem());
  }
  CHECK(foundIndicator);

  states.osdVolumeOpen.set(false);
  CHECK(osd->osdLoader->item.peek() == nullptr);
  drainDeferred();  // the unloaded panel is destroyed here (deleteLater)
}

TEST("osd: Config writes exactly the config.json Quickshell writes for the defaults") {
  (void)scratch();
  auto& config = common::Config::instance();
  (void)config;
  drainDeferred();
  const auto file = scratch() / "config" / "illogical-impulse" / "config.json";
  std::ifstream in(file);
  CHECK(in.good());
  std::stringstream text;
  text << in.rdbuf();
  const std::string json = text.str();
  // Byte for byte what Quickshell writes for the same Config.qml (tests/diff/qs_config_defaults.sh).
  std::ifstream expectedIn(std::string(II_DIFF_EXPECTED_DIR) + "/config-defaults.json");
  std::stringstream expected;
  expected << expectedIn.rdbuf();
  CHECK(!expected.str().empty());
  CHECK(json == expected.str());
}

TEST("osd: Config becomes ready once the watcher sees the file it wrote") {
  auto& config = common::Config::instance();
  // Quickshell: watchChanges -> fileChanged -> fileReloadTimer -> reload() -> onLoaded -> ready.
  CHECK(ii_test::pumpUntil([&] { return config.ready.peek(); }));
}

TEST_MAIN()
