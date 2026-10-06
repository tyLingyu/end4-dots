#pragma once

#include "runtime/component.h"
#include "runtime/item.h"

#include <functional>
#include <string>
#include <string_view>

namespace ii {

  // QML Loader, as QQuickLoader behaves:
  // - loads once the component is complete, and again whenever active, sourceComponent or
  //   source changes; the previous item is destroyed later (deleteLater), since unloading is
  //   often triggered from inside the item's own handlers;
  // - a loaded Item becomes the loader's visual child; the loader takes the item's size unless
  //   the loader was sized explicitly, in which case the item takes the loader's size;
  // - other objects (a PanelWindow) are just created and owned.
  class Loader : public Item {
  public:
    enum Status { Null = 0, Ready = 1, Loading = 2, Error = 3 };

    Loader();
    ~Loader() override;

    Property<bool> active{true};
    Property<Component<Object>> sourceComponent;
    // A URL as the QML gives it: relative to the declaring file (see setSourceBase), file:// or
    // a path relative to the shell root.
    Property<std::string> source;
    Property<Object*> item;  // read-only
    Property<int> status{Null};
    Property<bool> asynchronous;
    Signal<> loaded;

    // The QML file (relative to the shell root) that declares this loader, for relative sources.
    void setSourceBase(std::string qmlFile) { m_sourceBase = std::move(qmlFile); }

    // How `source` URLs find generated components: shell-relative QML path -> component. Set
    // by the program from qml2cpp's table.
    using Resolver = std::function<Component<Object>(std::string_view qmlPath)>;
    static void setResolver(Resolver resolver);

  protected:
    void componentComplete() override;

  private:
    void reload();
    void unload();
    [[nodiscard]] std::string resolvePath(std::string_view url) const;

    std::string m_sourceBase;
    bool m_sizedByItem = false;
  };

} // namespace ii
