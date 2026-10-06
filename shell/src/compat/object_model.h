#pragma once

// Quickshell ObjectModel (UntypedObjectModel): an ordered list of objects that reports insertions
// and removals.

#include "runtime/object.h"
#include "runtime/property.h"

#include <algorithm>
#include <vector>

namespace ii::qs {

  class UntypedObjectModel : public Object {
  public:
    Property<std::vector<Object*>> values;  // read-only
    Signal<Object*, int> objectInsertedPre;
    Signal<Object*, int> objectInsertedPost;
    Signal<Object*, int> objectRemovedPre;
    Signal<Object*, int> objectRemovedPost;

    [[nodiscard]] int indexOf(Object* object) const {
      const auto& list = values.peek();
      const auto it = std::ranges::find(list, object);
      return it == list.end() ? -1 : static_cast<int>(it - list.begin());
    }

    // For the services that fill the model.
    void insertObject(Object* object, int index = -1) {
      auto list = values.peek();
      const int at = index < 0 || index > static_cast<int>(list.size()) ? static_cast<int>(list.size()) : index;
      objectInsertedPre.emit(object, at);
      list.insert(list.begin() + at, object);
      values.writeDirect(std::move(list));
      objectInsertedPost.emit(object, at);
    }

    void removeObject(Object* object) {
      const int at = indexOf(object);
      if (at < 0) {
        return;
      }
      objectRemovedPre.emit(object, at);
      auto list = values.peek();
      list.erase(list.begin() + at);
      values.writeDirect(std::move(list));
      objectRemovedPost.emit(object, at);
    }
  };

} // namespace ii::qs
