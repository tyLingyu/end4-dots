#include "compat/pipewire.h"

namespace ii::qs {

  // The PipeWire connection comes in stage 3b; until then there are no nodes and no defaults.
  Pipewire& Pipewire::instance() {
    static Pipewire* self = [] {
      auto* p = new Pipewire();
      p->complete();
      return p;
    }();
    return *self;
  }

  Pipewire::Pipewire() {
    nodes.set(create<UntypedObjectModel>());
    links.set(create<UntypedObjectModel>());
    linkGroups.set(create<UntypedObjectModel>());
  }

} // namespace ii::qs
