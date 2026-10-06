#pragma once

#include "runtime/property.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ii {

  // QtObject: the ownership tree. An Object owns the objects created through it and destroys
  // them (newest first) before itself. Visual parenting is separate and lives in Item.
  class Object {
  public:
    Object() = default;
    virtual ~Object();

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;

    Property<std::string> objectName;

    [[nodiscard]] Object* owner() const noexcept { return m_owner; }

    template <typename T, typename... Args> T* create(Args&&... args) {
      auto object = std::make_unique<T>(std::forward<Args>(args)...);
      T* raw = object.get();
      adopt(std::move(object));
      return raw;
    }

    void adopt(std::unique_ptr<Object> object);
    std::unique_ptr<Object> release(Object* object);

    // QObject::deleteLater / QML destroy(): the owner gives the object up now, and it is destroyed
    // once the current event has been handled, so it is safe from inside the object's own
    // handlers. An object without an owner belongs to whoever holds it and is left alone.
    void deleteLater();
    void destroy() { deleteLater(); }

    // Marks construction as finished (QML: the component is completed), owned objects first,
    // then runs componentComplete() and emits completed (`Component.onCompleted`). Generated
    // code calls this once on the root of each instantiated component. Idempotent.
    void complete();
    [[nodiscard]] bool isCompleted() const noexcept { return m_completed; }
    Signal<> completed;

  protected:
    virtual void componentComplete() {}

    // Destroys every owned object now. Derived destructors call this when owned objects must
    // go before the derived part is torn down (Item: children before its scene node).
    void destroyOwned() noexcept;

  private:
    Object* m_owner = nullptr;
    std::vector<std::unique_ptr<Object>> m_owned;
    bool m_completed = false;
  };

  // A list of derived-object pointers as a list of base pointers (Quickshell.screens as a
  // Variants model).
  template <typename To, typename From> [[nodiscard]] std::vector<To*> upcastAll(const std::vector<From*>& list) {
    return std::vector<To*>(list.begin(), list.end());
  }

} // namespace ii
