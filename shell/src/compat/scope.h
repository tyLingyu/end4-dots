#pragma once

// Quickshell Scope and Singleton: plain containers (their children are owned objects).

#include "runtime/object.h"
#include "runtime/property.h"

#include <string>

namespace ii::qs {

  class Scope : public Object {
  public:
    Property<std::string> reloadableId;
  };

  // The root of a `pragma Singleton` component.
  class Singleton : public Scope {};

} // namespace ii::qs
