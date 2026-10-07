#pragma once

#include "runtime/object.h"

#include <functional>
#include <memory>

namespace ii {

  // A QML `Component { T { ... } }`: makes configured instances of T on demand. Generated code
  // builds the factory (the object tree and its bindings); createObject() creates the object
  // owned by `parent`, applies initial properties and completes it.
  template <typename T> class Component {
  public:
    using Factory = std::function<T*(Object& parent)>;

    Component() = default;
    explicit Component(Factory factory) : m_factory(std::make_shared<Factory>(std::move(factory))) {}

    // `component.createObject(parent, { a: x })` is createObject(parent, [&](T& o) { o.a.set(x); }):
    // initial properties replace the component's bindings for them and are in place before
    // Component.onCompleted, as in QML.
    T* createObject(Object& parent, const std::function<void(T&)>& initialProperties = {}) const {
      if (!m_factory) {
        return nullptr;
      }
      CreationScope creation;
      T* object = (*m_factory)(parent);
      if (initialProperties) {
        initialProperties(*object);
      }
      creation.finish();
      object->complete();
      return object;
    }
    [[nodiscard]] explicit operator bool() const noexcept { return m_factory != nullptr; }

    // Identity: two components are equal when they are the same component.
    friend bool operator==(const Component& a, const Component& b) noexcept { return a.m_factory == b.m_factory; }

  private:
    std::shared_ptr<Factory> m_factory;
  };

  // A component instance with no parent (a root): constructed, its bindings evaluated, completed.
  template <typename T> std::unique_ptr<T> createRoot() {
    CreationScope creation;
    auto object = std::make_unique<T>();
    creation.finish();
    object->complete();
    return object;
  }

} // namespace ii
