// FileView.watchChanges: fileChanged for in-place writes, atomic replacement (rename over the
// file, then the new file is watched), deletion, and creation of a file that didn't exist.

#include "compat/file_view.h"

#include "../check.h"
#include "../pump.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>

using namespace ii;

namespace {

  std::filesystem::path scratch() {
    static const auto dir = [] {
      auto path = std::filesystem::temp_directory_path() / ("ii-fileview-test-" + std::to_string(::getpid()));
      std::filesystem::remove_all(path);
      std::filesystem::create_directories(path);
      return path;
    }();
    return dir;
  }

  void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
  }

  struct Watched {
    std::unique_ptr<qs::FileView> view = std::make_unique<qs::FileView>();
    int changes = 0;

    explicit Watched(const std::filesystem::path& path) {
      view->path.set(path.string());
      view->watchChanges.set(true);
      view->fileChanged.connectForever([this] { ++changes; });
      view->complete();
      ii_test::drainDeferred();
    }

    // Waits for the next fileChanged, then lets coalesced follow-ups arrive.
    bool next() {
      const int before = changes;
      const bool got = ii_test::pumpUntil([&] { return changes > before; });
      ii_test::pumpFor(30);
      return got;
    }
  };

} // namespace

TEST("fileview: in-place write and atomic replacement report fileChanged, and keep watching") {
  const auto file = scratch() / "a.json";
  writeFile(file, "1");
  Watched w(file);

  writeFile(file, "2");
  CHECK(w.next());

  const auto tmp = scratch() / "a.json.tmp";
  writeFile(tmp, "3");
  std::filesystem::rename(tmp, file);
  CHECK(w.next());
  CHECK(w.view->text() == "1");  // fileChanged alone doesn't reload, as in Quickshell

  // The replacement is the file now watched.
  const int before = w.changes;
  writeFile(file, "4");
  CHECK(w.next());
  CHECK(w.changes > before);
}

TEST("fileview: a file created after the watch started reports fileChanged") {
  const auto file = scratch() / "later.json";
  Watched w(file);
  ii_test::pumpFor(30);
  CHECK(w.changes == 0);

  writeFile(file, "{}");
  CHECK(w.next());

  // And it is watched from then on.
  writeFile(file, "{\"a\":1}");
  CHECK(w.next());
}

TEST("fileview: deletion reports fileChanged; nothing after watchChanges is turned off") {
  const auto file = scratch() / "gone.json";
  writeFile(file, "x");
  Watched w(file);

  std::filesystem::remove(file);
  CHECK(w.next());

  w.view->watchChanges.set(false);
  const int before = w.changes;
  writeFile(file, "y");
  ii_test::pumpFor(100);
  CHECK(w.changes == before);
}

TEST("fileview: reload from fileChanged reads the new contents") {
  const auto file = scratch() / "reload.json";
  writeFile(file, "old");
  Watched w(file);
  int loads = 0;
  w.view->loadedSignal.connectForever([&] { ++loads; });
  w.view->fileChanged.connectForever([&] { w.view->reload(); });

  writeFile(file, "new");
  CHECK(ii_test::pumpUntil([&] { return loads > 0 && w.view->text() == "new"; }));
}

TEST_MAIN()
