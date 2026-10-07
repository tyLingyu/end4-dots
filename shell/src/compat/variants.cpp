#include "compat/variants.h"

#include <algorithm>

namespace ii::qs {

  Variants::Variants() {
    model.changed().connectForever([this] { update(); });
  }

  void Variants::setDelegate(Delegate delegate) {
    m_delegate = std::move(delegate);
    update();
  }

  void Variants::componentComplete() { update(); }

  void Variants::update() {
    if (!isCompleted() || !m_delegate) {
      return;
    }
    const auto& items = model.peek();
    // Drop instances whose item left the model; keep the others; create the new ones in order.
    std::vector<std::pair<Object*, Object*>> kept;
    for (auto& [item, instance] : m_instances) {
      if (std::ranges::find(items, item) != items.end()) {
        kept.emplace_back(item, instance);
      } else {
        instance->deleteLater();
      }
    }
    std::vector<std::pair<Object*, Object*>> next;
    std::vector<Object*> list;
    for (Object* item : items) {
      auto it = std::ranges::find(kept, item, &std::pair<Object*, Object*>::first);
      Object* instance = nullptr;
      if (it != kept.end()) {
        instance = it->second;
      } else {
        // The delegate builds the tree; evaluating its bindings and completing it is the runtime's.
        CreationScope creation;
        instance = m_delegate(*this, item);
        creation.finish();
        if (instance != nullptr) {
          instance->complete();
        }
      }
      if (instance != nullptr) {
        next.emplace_back(item, instance);
        list.push_back(instance);
      }
    }
    m_instances = std::move(next);
    instances.writeDirect(std::move(list));
  }

} // namespace ii::qs
