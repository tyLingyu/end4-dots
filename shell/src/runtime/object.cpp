#include "runtime/object.h"

#include <algorithm>

namespace ii {

  Object::~Object() { destroyOwned(); }

  void Object::adopt(std::unique_ptr<Object> object) {
    object->m_owner = this;
    m_owned.push_back(std::move(object));
  }

  std::unique_ptr<Object> Object::release(Object* object) {
    auto it = std::ranges::find_if(m_owned, [object](const auto& owned) { return owned.get() == object; });
    if (it == m_owned.end()) {
      return nullptr;
    }
    auto released = std::move(*it);
    m_owned.erase(it);
    released->m_owner = nullptr;
    return released;
  }

  void Object::destroyOwned() noexcept {
    // Newest first, popping one at a time: a destructor may release or create siblings.
    while (!m_owned.empty()) {
      auto last = std::move(m_owned.back());
      m_owned.pop_back();
      last.reset();
    }
  }

} // namespace ii
