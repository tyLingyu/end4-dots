#pragma once

// Quickshell.Services.Pipewire.

#include "compat/object_model.h"
#include "runtime/js.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ii::qs {

  // qs::service::pipewire::PwAudioChannel (the SPA channel positions).
  struct PwAudioChannel {
    enum Enum { Unknown = 0, NA, Mono, FrontCenter, FrontLeft, FrontRight };
  };

  class PwNodeAudio : public Object {
  public:
    Property<bool> muted;
    Property<double> volume;  // average of `volumes`; changes are notified through volumes.changed()
    Property<std::vector<int>> channels;  // read-only, PwAudioChannel::Enum values
    Property<std::vector<double>> volumes;
  };

  class PwNode : public Object {
  public:
    Property<int> id;
    Property<std::string> name;
    Property<std::string> description;
    Property<std::string> nickname;
    Property<bool> isSink;
    Property<bool> isStream;
    Property<int> type;
    Property<js::Json> properties;
    Property<PwNodeAudio*> audio;
    Property<bool> ready;
  };

  class Pipewire : public Object {
  public:
    static Pipewire& instance();

    Property<UntypedObjectModel*> nodes;
    Property<UntypedObjectModel*> links;
    Property<UntypedObjectModel*> linkGroups;
    Property<PwNode*> defaultAudioSink;    // read-only
    Property<PwNode*> defaultAudioSource;  // read-only
    Property<PwNode*> preferredDefaultAudioSink;
    Property<PwNode*> preferredDefaultAudioSource;
    Property<bool> ready;

  private:
    Pipewire();
  };

  // Keeps the given nodes bound so their properties stay current.
  class PwObjectTracker : public Object {
  public:
    Property<std::vector<Object*>> objects;
  };

} // namespace ii::qs
