#pragma once

// Quickshell.Services.Pipewire compatibility layer.
// As Quickshell's src/services/pipewire (revision 7511545).

#include "compat/object_model.h"
#include "runtime/js.h"
#include "runtime/object.h"
#include "runtime/property.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct pw_node;
struct pw_metadata;

namespace ii::qs {

  // qs::service::pipewire::PwAudioChannel (the SPA channel positions).
  // Matches Quickshell node.hpp:PwAudioChannel
  struct PwAudioChannel {
    enum Enum {
      Unknown = 0,
      NA = 1,
      Mono = 2,
      FrontCenter = 3,
      FrontLeft = 4,
      FrontRight = 5,
    };
  };

  // qs::service::pipewire::PwNodeType (media.class flags)
  // Matches Quickshell node.hpp:PwNodeType
  struct PwNodeType {
    enum Flag {
      Untracked = 0,
      Audio = 1 << 0,
      Video = 1 << 1,
      Stream = 1 << 2,
      Source = 1 << 3,
      Sink = 1 << 4,
      AudioSink = Audio | Sink,
      AudioSource = Audio | Source,
      AudioDuplex = Audio | Sink | Source,
      AudioOutStream = Audio | Sink | Stream,
      AudioInStream = Audio | Source | Stream,
      VideoSource = Video | Source,
      VideoSink = Video | Sink,
    };
  };

  class PwNode;
  class Pipewire;

  // As Quickshell's PwNodeAudioIface / PwNodeBoundAudio (node.hpp, qml.hpp).
  class PwNodeAudio : public Object {
  public:
    PwNodeAudio();
    ~PwNodeAudio() override;

    Property<bool> muted;
    Property<double> volume;  // average of `volumes`; changes are notified through volumes.changed()
    Property<std::vector<int>> channels;  // read-only, PwAudioChannel::Enum values
    Property<std::vector<double>> volumes;

    void init(PwNode* node);
    [[nodiscard]] double averageVolume() const noexcept;
    void setAverageVolume(double volume);
    void setVolumes(const std::vector<double>& vols);
    void setMuted(bool mute);

    // Direct setter used by unit tests or server updates
    void updateFromServer(bool mute, const std::vector<int>& ch, const std::vector<double>& vols);

  private:
    PwNode* m_node = nullptr;

    struct MutedInterceptor;
    struct VolumeInterceptor;
    struct VolumesInterceptor;
    std::unique_ptr<MutedInterceptor> m_mutedInterceptor;
    std::unique_ptr<VolumeInterceptor> m_volumeInterceptor;
    std::unique_ptr<VolumesInterceptor> m_volumesInterceptor;
  };

  // As Quickshell's PwNode / PwNodeIface (node.hpp, qml.hpp).
  class PwNode : public Object {
  public:
    friend class Pipewire;

    PwNode();
    ~PwNode() override;

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

    void ref();
    void unref();
    [[nodiscard]] int refCount() const noexcept { return m_refCount; }

    [[nodiscard]] pw_node* proxy() const noexcept { return m_proxy; }
    void setProxy(pw_node* p) noexcept { m_proxy = p; }

    void bind();
    void unbind();

    [[nodiscard]] bool shouldUseDevice() const noexcept;
    void setDeviceVolumes(const std::vector<double>& vols);
    void setDeviceMuted(bool mute);

    // Internal callbacks called by Backend
    void onInfo(const void* info);
    void onParam(int seq, uint32_t paramId, uint32_t index, uint32_t next, const void* param);
    void onSyncDone(int seq);

    Signal<PwNode*> destroying;

    struct Impl;
    [[nodiscard]] Impl* impl() const noexcept { return m_impl.get(); }

  private:
    int m_refCount = 0;
    pw_node* m_proxy = nullptr;
    int m_syncSeq = 0;
    std::unique_ptr<Impl> m_impl;
  };

  // As Quickshell's Pipewire singleton (qml.hpp, defaults.hpp, registry.hpp).
  class Pipewire : public Object {
  public:
    static Pipewire& instance();
    ~Pipewire() override;

    Property<UntypedObjectModel*> nodes;
    Property<UntypedObjectModel*> links;
    Property<UntypedObjectModel*> linkGroups;
    Property<PwNode*> defaultAudioSink;    // read-only
    Property<PwNode*> defaultAudioSource;  // read-only
    Property<PwNode*> preferredDefaultAudioSink;
    Property<PwNode*> preferredDefaultAudioSource;
    Property<bool> ready;

    [[nodiscard]] PwNode* findNodeByName(std::string_view name) const;
    [[nodiscard]] PwNode* findNodeById(int id) const;

    void changeConfiguredSink(PwNode* node);
    void changeConfiguredSource(PwNode* node);

    // Pure logic helpers for testing cubic conversions
    static double linearToVisual(float linear) noexcept;
    static float visualToLinear(double visual) noexcept;

    struct Backend;

  private:
    Pipewire();

    struct PreferredSinkInterceptor;
    struct PreferredSourceInterceptor;
    std::unique_ptr<PreferredSinkInterceptor> m_sinkInterceptor;
    std::unique_ptr<PreferredSourceInterceptor> m_sourceInterceptor;

    std::unique_ptr<Backend> m_backend;
  };

  // Keeps the given nodes bound so their properties stay current.
  // As Quickshell's PwObjectTracker (qml.hpp).
  class PwObjectTracker : public Object {
  public:
    PwObjectTracker();
    ~PwObjectTracker() override;

    Property<std::vector<Object*>> objects;

  private:
    void updateTracked();
    void objectDestroyed(Object* obj);

    std::vector<Object*> m_tracked;
    std::vector<Connection> m_destroyedConns;
  };

} // namespace ii::qs
