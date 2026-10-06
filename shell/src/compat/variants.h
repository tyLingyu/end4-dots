#pragma once

// Quickshell Variants: one delegate instance per model item, with `modelData` set. Instances
// whose item is still in the model are kept when the model changes.

#include "runtime/object.h"
#include "runtime/property.h"

#include <functional>
#include <vector>

namespace ii::qs {

  class Variants : public Object {
  public:
    // Creates an instance for a model item, owned by `owner`, with modelData set.
    using Delegate = std::function<Object*(Object& owner, Object* modelData)>;

    Variants();

    Property<std::vector<Object*>> model;
    Property<std::vector<Object*>> instances;  // read-only

    void setDelegate(Delegate delegate);

  protected:
    void componentComplete() override;

  private:
    void update();

    Delegate m_delegate;
    std::vector<std::pair<Object*, Object*>> m_instances;  // model item -> instance
  };

} // namespace ii::qs
