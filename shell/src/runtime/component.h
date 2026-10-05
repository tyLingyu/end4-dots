#pragma once

#include "runtime/object.h"

#include <functional>
#include <memory>

namespace ii {

  // A QML `Component { T { ... } }`: makes configured instances of T on demand. Generated code
  // builds the factory; `createObject(parent)` creates the object owned by `parent`, applies the
  // component's bindings and completes it.
  template <typename T> class Component {
  public:
    using Factory = std::function<T*(Object& parent)>;

    Component() = default;
    explicit Component(Factory factory) : m_factory(std::make_shared<Factory>(std::move(factory))) {}

    [[nodiscard]] T* createObject(Object& parent) const { return m_factory ? (*m_factory)(parent) : nullptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_factory != nullptr; }

    // Identity: two components are equal when they are the same component.
    friend bool operator==(const Component& a, const Component& b) noexcept { return a.m_factory == b.m_factory; }

  private:
    std::shared_ptr<Factory> m_factory;
  };

} // namespace ii
