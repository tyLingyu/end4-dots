#include "compat/file_view.h"

#include "core/deferred_call.h"
#include "core/log.h"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <sys/stat.h>

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

  JsonAdapter::JsonAdapter() {
    propertyChanged.connectForever([this] {
      if (!m_deserializing) {
        adapterUpdated.emit();
      }
    });
  }

  bool JsonAdapter::deserialize(const std::string& text) {
    js::Json json;
    try {
      json = js::parse(text);
    } catch (const std::exception&) {
      return false;
    }
    m_deserializing = true;
    readJson(json);
    m_deserializing = false;
    return true;
  }

  std::string JsonAdapter::serialize() const {
    js::Json json;
    writeJson(json);
    return js::stringify(json, 4) + "\n";
  }

  // ── FileView ───────────────────────────────────────────────────────────────

  FileView::FileView() {
    path.changed().connectForever([this] {
      if (isCompleted()) {
        load();
      }
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

  FileView::~FileView() = default;

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

  void FileView::reload() { load(); }

  void FileView::writeAdapter() {
    if (FileViewAdapter* a = adapter.peek()) {
      write(a->serialize());
    }
  }

  void FileView::updateWatch() {
    if (watchChanges.peek()) {
      kLog.debug("watchChanges is not implemented yet (stage 3b)");
    }
  }

} // namespace ii::qs
