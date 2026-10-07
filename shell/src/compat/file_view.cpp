#include "compat/file_view.h"

#include "compat/json_value.h"

#include "core/deferred_call.h"
#include "core/log.h"
#include "runtime/fd_watch.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <poll.h>
#include <sstream>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace ii::qs {

  namespace {

    constexpr Logger kLog("fileview");

    std::string stripFileUrl(const std::string& path) { return path.starts_with("file://") ? path.substr(7) : path; }

    // Reads a file, or the reason it can't be read.
    FileViewError::Enum readFile(const std::string& path, std::string& out) {
      struct stat st{};
      if (::stat(path.c_str(), &st) != 0) {
        return errno == EACCES ? FileViewError::PermissionDenied : FileViewError::FileNotFound;
      }
      if (!S_ISREG(st.st_mode)) {
        return FileViewError::NotAFile;
      }
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        return FileViewError::PermissionDenied;
      }
      std::ostringstream text;
      text << in.rdbuf();
      out = text.str();
      return FileViewError::Success;
    }

  } // namespace

  // ── JsonAdapter ────────────────────────────────────────────────────────────

  void JsonAdapter::componentComplete() { connectNotifiers([this] { onPropertyChanged(); }); }

  void JsonAdapter::onPropertyChanged() {
    if (m_changesBlocked) {
      return;
    }
    connectNotifiers([this] { onPropertyChanged(); });  // a replaced nested object
    adapterUpdated.emit();
  }

  bool JsonAdapter::deserialize(const std::string& text) {
    if (text.empty()) {
      return true;
    }
    js::Json json;
    try {
      json = js::parse(text);
    } catch (const std::exception& e) {
      kLog.warn("Failed to deserialize json: {}", e.what());
      return false;
    }
    if (!json.is_object()) {
      kLog.warn("Failed to deserialize json: not an object");
      return false;
    }
    m_changesBlocked = true;
    readJson(json);
    m_changesBlocked = false;
    connectNotifiers([this] { onPropertyChanged(); });
    return true;
  }

  std::string JsonAdapter::serialize() const {
    js::Json json;
    writeJson(json);
    return qtJsonIndented(json);
  }

  // ── FileView ───────────────────────────────────────────────────────────────

  FileView::FileView() {
    path.changed().connectForever([this] {
      if (isCompleted()) {
        load();
      }
      updateWatch();
    });
    adapter.changed().connectForever([this] {
      if (FileViewAdapter* a = adapter.peek()) {
        a->adapterUpdated.connectForever([this] { adapterUpdated.emit(); });
        if (m_hasContent) {
          a->deserialize(m_text);
        }
      }
    });
    watchChanges.changed().connectForever([this] { updateWatch(); });
  }

  FileView::~FileView() { stopWatch(); }

  void FileView::componentComplete() {
    if (!path.peek().empty() && preload.peek()) {
      load();
    }
  }

  void FileView::load() {
    m_text.clear();
    m_hasContent = false;
    loaded.writeDirect(false);
    const std::string file = stripFileUrl(path.peek());
    if (file.empty()) {
      return;
    }
    const FileViewError::Enum error = readFile(file, m_text);
    if (error != FileViewError::Success) {
      if (printErrors.peek()) {
        kLog.warn("failed to read {}", file);
      }
      // Reported from the event loop, as Quickshell's asynchronous load does.
      DeferredCall::callLater([this, alive = std::weak_ptr<bool>(m_alive), error, generation = path.peek()] {
        if (!alive.expired() && path.peek() == generation) {
          loadFailed.emit(error);
        }
      });
      return;
    }
    m_hasContent = true;
    if (FileViewAdapter* a = adapter.peek()) {
      a->deserialize(m_text);
    }
    DeferredCall::callLater([this, alive = std::weak_ptr<bool>(m_alive), generation = path.peek()] {
      if (!alive.expired() && path.peek() == generation && m_hasContent) {
        loaded.writeDirect(true);
        loadedSignal.emit();
      }
    });
  }

  std::string FileView::text() {
    if (!m_hasContent && !path.peek().empty()) {
      readFile(stripFileUrl(path.peek()), m_text);  // a blocking read, as FileView.text() forces
    }
    return m_text;
  }

  js::Json FileView::data() { return js::Json(text()); }

  void FileView::write(const std::string& content) {
    if (blockWrites.peek()) {
      return;
    }
    const std::string file = stripFileUrl(path.peek());
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);  // as Quickshell's mkpath
    if (ec) {
      kLog.warn("Write of {} failed: Could not create parent directories of file.", file);
      saveFailed.emit(FileViewError::PermissionDenied);
      return;
    }
    const std::string target = atomicWrites.peek() ? file + ".tmp" : file;
    {
      std::ofstream out(target, std::ios::binary | std::ios::trunc);
      if (!out) {
        saveFailed.emit(FileViewError::PermissionDenied);
        return;
      }
      out << content;
    }
    if (atomicWrites.peek() && std::rename(target.c_str(), file.c_str()) != 0) {
      saveFailed.emit(FileViewError::Unknown);
      return;
    }
    m_text = content;
    m_hasContent = true;
    saved.emit();
  }

  void FileView::setText(const std::string& text) { write(text); }
  void FileView::setData(const std::string& data) { write(data); }

  // Quickshell's reload() is updatePath(): a new load and a new watcher.
  void FileView::reload() {
    load();
    updateWatch();
  }

  void FileView::writeAdapter() {
    if (FileViewAdapter* a = adapter.peek()) {
      write(a->serialize());
    }
  }

  // Masks and event coalescing are Qt's inotify engine (qfilesystemwatcher_inotify.cpp); the
  // file/directory handling is Quickshell's (io/fileview.cpp, updateWatchedFiles and the two
  // onWatched*Changed slots).
  namespace {
    constexpr std::uint32_t kFileMask = IN_ATTRIB | IN_MODIFY | IN_MOVE | IN_MOVE_SELF | IN_DELETE_SELF;
    constexpr std::uint32_t kDirMask = IN_ATTRIB | IN_MOVE | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF;
    constexpr std::uint32_t kRemoved = IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT;
  } // namespace

  void FileView::stopWatch() {
    if (m_inotifyFd < 0) {
      return;
    }
    FdWatch::unwatch(m_watchId);
    ::close(m_inotifyFd);  // drops its watches and any unread events, as deleting the watcher does
    m_inotifyFd = -1;
    m_watchId = 0;
    m_fileWd = -1;
    m_dirWd = -1;
    ++m_watchGeneration;
  }

  void FileView::updateWatch() {
    stopWatch();
    m_watchedPath = stripFileUrl(path.peek());
    if (m_watchedPath.empty() || !watchChanges.peek()) {
      return;
    }
    m_inotifyFd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_inotifyFd < 0) {
      kLog.warn("inotify_init1 failed for {}", m_watchedPath);
      return;
    }
    // QFileSystemWatcher::addPath fails quietly for a path that doesn't exist.
    watchFile();
    std::string dir = m_watchedPath.contains('/') ? m_watchedPath : "./" + m_watchedPath;
    dir.resize(dir.rfind('/'));
    const int wd = dir.empty() ? -1 : ::inotify_add_watch(m_inotifyFd, dir.c_str(), kDirMask);
    m_dirWd = wd >= 0 && wd != m_fileWd ? wd : -1;
    m_watchId = FdWatch::watch(m_inotifyFd, POLLIN, [this](short) { onWatchEvents(); });
  }

  void FileView::watchFile() {
    struct stat st{};
    if (::stat(m_watchedPath.c_str(), &st) != 0) {
      return;
    }
    // QFileSystemWatcher keeps a directory path in directories(), never in files().
    const int wd = ::inotify_add_watch(m_inotifyFd, m_watchedPath.c_str(), S_ISDIR(st.st_mode) ? kDirMask : kFileMask);
    m_fileWd = wd >= 0 && !S_ISDIR(st.st_mode) ? wd : -1;
  }

  void FileView::onWatchEvents() {
    // Qt reads everything available and merges the masks of events for the same watch.
    alignas(inotify_event) char buffer[4096];
    std::vector<std::pair<int, std::uint32_t>> merged;  // in first-seen order
    while (true) {
      const ssize_t n = ::read(m_inotifyFd, buffer, sizeof(buffer));
      if (n <= 0) {
        break;
      }
      for (ssize_t at = 0; at < n;) {
        const auto* event = reinterpret_cast<const inotify_event*>(buffer + at);
        auto it = std::ranges::find(merged, event->wd, &std::pair<int, std::uint32_t>::first);
        if (it != merged.end()) {
          it->second |= event->mask;
        } else {
          merged.emplace_back(event->wd, event->mask);
        }
        at += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
      }
    }
    const std::uint64_t generation = m_watchGeneration;
    const std::weak_ptr<bool> alive = m_alive;
    for (const auto& [wd, mask] : merged) {
      // A fileChanged handler may reload (a new watcher) or destroy this view.
      if (alive.expired() || generation != m_watchGeneration) {
        return;
      }
      const bool removed = (mask & kRemoved) != 0;
      if (wd >= 0 && wd == m_fileWd) {
        if (removed) {
          ::inotify_rm_watch(m_inotifyFd, wd);
          m_fileWd = -1;
        }
        // onWatchedFileChanged: watch the path again (a replaced file), then report.
        if (m_fileWd < 0) {
          watchFile();
        }
        fileChanged.emit();
      } else if (wd >= 0 && wd == m_dirWd) {
        if (removed) {
          ::inotify_rm_watch(m_inotifyFd, wd);
          m_dirWd = -1;
        }
        // onWatchedDirectoryChanged: the file was just created.
        if (m_fileWd < 0) {
          watchFile();
          if (m_fileWd >= 0) {
            fileChanged.emit();
          }
        }
      }
    }
  }

} // namespace ii::qs
