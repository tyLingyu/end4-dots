#pragma once

// Quickshell.Io FileView, FileViewAdapter, JsonAdapter, JsonObject.
//
// JsonAdapter/JsonObject are QML components whose properties mirror a JSON document. Quickshell
// reflects over QML properties; generated classes can't, so qml2cpp generates writeJson/readJson
// for every class derived from these (property name -> value, nested JsonObjects recursively).

#include "runtime/js.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ii::qs {

  // qs::io::FileViewError
  struct FileViewError {
    enum Enum { Success = 0, Unknown = 1, FileNotFound = 2, PermissionDenied = 3, NotAFile = 4 };
  };

  // A JSON object whose fields are the component's properties. qml2cpp generates the overrides
  // for every class derived from JsonObject or JsonAdapter, listing its declared properties
  // (Quickshell reflects over QML properties; generated classes can't).
  class JsonObject : public Object {
  public:
    // Every declared property, nested objects recursively (QJsonValue::fromVariant values).
    virtual void writeJson(js::Json& out) const { out = js::Json::object(); }
    // The properties `in` has, converted as QVariant::convert does; a write keeps bindings.
    virtual void readJson(const js::Json& in) { (void)in; }
    // Calls `notify` on every property change, nested objects too. Once per object.
    virtual void connectNotifiers(const std::function<void()>& notify) { (void)notify; }
    // Dynamic access, for code that indexes by name (Config.setNestedValue): a nested object,
    // a value, and an assignment (which breaks the property's binding, as JS's does).
    [[nodiscard]] virtual JsonObject* child(std::string_view name) const {
      (void)name;
      return nullptr;
    }
    [[nodiscard]] virtual js::Json value(std::string_view name) const {
      (void)name;
      return js::Json();
    }
    virtual bool setValue(std::string_view name, const js::Json& value) {
      (void)name;
      (void)value;
      return false;
    }

  protected:
    bool m_notifiersConnected = false;
  };

  class FileViewAdapter : public JsonObject {
  public:
    Signal<> adapterUpdated;
    // The file's content became `text`; false when it can't be parsed.
    virtual bool deserialize(const std::string& text) = 0;
    [[nodiscard]] virtual std::string serialize() const = 0;
  };

  // The root of a JSON-backed file (Quickshell's JsonAdapter): reads the document into the
  // properties, reports any property change as adapterUpdated, writes QJsonDocument::Indented.
  class JsonAdapter : public FileViewAdapter {
  public:
    bool deserialize(const std::string& text) override;
    [[nodiscard]] std::string serialize() const override;

  protected:
    void componentComplete() override;

  private:
    void onPropertyChanged();

    bool m_changesBlocked = false;
  };

  // A file's contents. Loads asynchronously unless blockLoading; reloads when watchChanges and
  // the file changes on disk; writes through setText/setData or, with an adapter, writeAdapter.
  class FileView : public Object {
  public:
    FileView();
    ~FileView() override;

    Property<std::string> path;
    Property<bool> preload{true};
    Property<bool> blockLoading;
    Property<bool> blockAllReads;
    Property<bool> blockWrites;
    Property<bool> atomicWrites{true};
    Property<bool> watchChanges;
    Property<bool> printErrors{true};
    Property<bool> loaded;  // read-only
    Property<FileViewAdapter*> adapter;

    Signal<> loadedSignal;
    Signal<FileViewError::Enum> loadFailed;
    Signal<> saved;
    Signal<FileViewError::Enum> saveFailed;
    Signal<> fileChanged;
    Signal<> adapterUpdated;

    [[nodiscard]] std::string text();
    [[nodiscard]] js::Json data();
    void setText(const std::string& text);
    void setData(const std::string& data);
    void reload();
    void writeAdapter();
    bool waitForJob() { return true; }

  protected:
    void componentComplete() override;

  private:
    void load();
    void write(const std::string& content);
    // Quickshell's updateWatchedFiles: a QFileSystemWatcher on the file and its directory,
    // recreated whenever the path is (re)loaded or watchChanges changes.
    void updateWatch();
    void stopWatch();
    void onWatchEvents();
    void watchFile();

    std::string m_text;
    bool m_hasContent = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);  // expires deferred reports
    int m_inotifyFd = -1;
    std::uint64_t m_watchId = 0;
    std::uint64_t m_watchGeneration = 0;  // bumped by stopWatch, so a handler can tell it was replaced
    std::string m_watchedPath;
    int m_fileWd = -1;
    int m_dirWd = -1;
  };

} // namespace ii::qs
