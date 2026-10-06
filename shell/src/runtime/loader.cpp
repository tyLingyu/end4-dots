#include "runtime/loader.h"

#include "core/log.h"
#include "runtime/qt.h"

namespace ii {

  namespace {
    constexpr Logger kLog("loader");

    Loader::Resolver& resolver() {
      static Loader::Resolver instance;
      return instance;
    }
  } // namespace

  void Loader::setResolver(Resolver r) { resolver() = std::move(r); }

  Loader::Loader() {
    active.changed().connectForever([this] { reload(); });
    sourceComponent.changed().connectForever([this] { reload(); });
    source.changed().connectForever([this] { reload(); });
  }

  Loader::~Loader() { destroyOwned(); }  // the item is owned: destroyed with the loader, no deleteLater

  void Loader::componentComplete() { reload(); }

  std::string Loader::resolvePath(std::string_view url) const {
    const std::string resolved = qt::resolvedUrl(url, m_sourceBase);  // file:///<root>/<path>
    const std::string prefix = "file://" + shellRoot() + "/";
    return resolved.starts_with(prefix) ? resolved.substr(prefix.size()) : resolved;
  }

  void Loader::unload() {
    Object* old = item.peek();
    if (old == nullptr) {
      return;
    }
    item.writeDirect(nullptr);
    status.writeDirect(Null);
    // The size bindings read the item, which is going away; unloaded, the loader is 0 x 0.
    if (m_sizedByItem) {
      implicitWidth.set(0.0);
      implicitHeight.set(0.0);
      m_sizedByItem = false;
    }
    if (auto* oldItem = dynamic_cast<Item*>(old)) {
      oldItem->parent.set(nullptr);
    }
    old->deleteLater();
  }

  void Loader::reload() {
    if (!isCompleted()) {
      return;
    }
    unload();
    if (!active.peek()) {
      return;
    }
    Component<Object> component = sourceComponent.peek();
    if (!component && !source.peek().empty()) {
      const std::string path = resolvePath(source.peek());
      component = resolver() ? resolver()(path) : Component<Object>();
      if (!component) {
        kLog.warn("no generated component for {}", path);
        status.writeDirect(Error);
        return;
      }
    }
    if (!component) {
      return;
    }
    Object* object = component.createObject(*this, [this](Object& created) {
      // Before completion, so the item's onCompleted handlers already see their visual parent.
      if (auto* child = dynamic_cast<Item*>(&created)) {
        child->parent.set(this);
      }
    });
    if (auto* child = dynamic_cast<Item*>(object)) {
      m_sizedByItem = true;
      if (followsImplicitSize(Axis::Horizontal)) {
        implicitWidth.bind([child] { return child->width.get(); }, "Loader.implicitWidth (item)");
      } else {
        child->width.bind([this] { return width.get(); }, "Loader.item.width");
      }
      if (followsImplicitSize(Axis::Vertical)) {
        implicitHeight.bind([child] { return child->height.get(); }, "Loader.implicitHeight (item)");
      } else {
        child->height.bind([this] { return height.get(); }, "Loader.item.height");
      }
    }
    item.writeDirect(object);
    status.writeDirect(object != nullptr ? Ready : Error);
    if (object != nullptr) {
      loaded.emit();
    }
  }

} // namespace ii
