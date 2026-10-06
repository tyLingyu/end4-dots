#pragma once

// Quickshell.Io FileView, FileViewAdapter, JsonAdapter, JsonObject.
//
// JsonAdapter/JsonObject are QML components whose properties mirror a JSON document. Quickshell
// reflects over QML properties; generated classes can't, so qml2cpp generates writeJson/readJson
// for every class derived from these (property name -> value, nested JsonObjects recursively).

#include "runtime/js.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <memory>
#include <string>

namespace ii::qs {

  // qs::io::FileViewError
  struct FileViewError {
    enum Enum { Success = 0, Unknown = 1, FileNotFound = 2, PermissionDenied = 3, NotAFile = 4 };
  };

  // A JSON object whose fields are the component's properties.
  class JsonObject : public Object {
  public:
    // Generated: every declared property, nested objects recursively.
    virtual void writeJson(js::Json& out) const { out = js::Json::object(); }
    virtual void readJson(const js::Json& in) { (void)in; }
    // Generated: emitted (also for nested objects) when a property changes.
    Signal<> propertyChanged;
  };

  class FileViewAdapter : public JsonObject {
  public:
    Signal<> adapterUpdated;
    // The file's content became `text`; returns false when it can't be parsed.
    virtual bool deserialize(const std::string& text) = 0;
    [[nodiscard]] virtual std::string serialize() const = 0;
  };

  // The root of a JSON-backed file. Serialization matches Quickshell's (QJsonDocument::Indented).
  class JsonAdapter : public FileViewAdapter {
  public:
    JsonAdapter();
    bool deserialize(const std::string& text) override;
    [[nodiscard]] std::string serialize() const override;

  private:
    bool m_deserializing = false;
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
    void updateWatch();

    std::string m_text;
    bool m_hasContent = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);  // expires deferred reports
    int m_watchFd = -1;
    std::uint64_t m_watchId = 0;
    int m_watchDescriptor = -1;
  };

} // namespace ii::qs
