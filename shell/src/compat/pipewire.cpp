#include "compat/pipewire.h"

// Quickshell PipeWire service compatibility implementation.
// Ported faithfully from Quickshell revision 7511545:
// - Connection & Loop: /tmp/qs/q/src/services/pipewire/core.cpp, connection.cpp
// - Registry & Globals: /tmp/qs/q/src/services/pipewire/registry.cpp
// - Node & Audio: /tmp/qs/q/src/services/pipewire/node.cpp
// - Device & Route: /tmp/qs/q/src/services/pipewire/device.cpp
// - Metadata & Defaults: /tmp/qs/q/src/services/pipewire/defaults.cpp, metadata.cpp
// - QML Interfaces & Tracker: /tmp/qs/q/src/services/pipewire/qml.cpp

#include "core/deferred_call.h"
#include "core/log.h"
#include "runtime/fd_watch.h"

#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wconversion"
#include <pipewire/device.h>
#include <pipewire/extensions/metadata.h>
#include <pipewire/keys.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/param/param.h>
#include <spa/param/props.h>
#include <spa/param/route.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/pod/parser.h>
#include <spa/utils/defs.h>
#include <spa/utils/dict.h>
#include <spa/utils/hook.h>
#include <spa/utils/keys.h>
#include <spa/utils/result.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ii::qs {

  namespace {
    constexpr Logger kLog("compat:pipewire");

    // As Quickshell node.hpp:PwVolumeProps / parseSpaPod
    struct PwVolumeProps {
      std::vector<int> channels;
      std::vector<double> volumes;
      bool mute = false;
      float volumeStep = -1.0f;

      static PwVolumeProps parseSpaPod(const spa_pod* param) {
        PwVolumeProps props;
        if (param == nullptr) {
          return props;
        }

        const auto* volumesProp = spa_pod_find_prop(param, nullptr, SPA_PROP_channelVolumes);
        const auto* channelsProp = spa_pod_find_prop(param, nullptr, SPA_PROP_channelMap);
        const auto* muteProp = spa_pod_find_prop(param, nullptr, SPA_PROP_mute);
        const auto* volumeStepProp = spa_pod_find_prop(param, nullptr, SPA_PROP_volumeStep);

        if (volumesProp != nullptr) {
          const auto* arr = reinterpret_cast<const spa_pod_array*>(&volumesProp->value);
          spa_pod* iter = nullptr;
          SPA_POD_ARRAY_FOREACH(arr, iter) {
            const float linear = *reinterpret_cast<const float*>(iter);
            props.volumes.push_back(Pipewire::linearToVisual(linear));
          }
        }

        if (channelsProp != nullptr) {
          const auto* arr = reinterpret_cast<const spa_pod_array*>(&channelsProp->value);
          spa_pod* iter = nullptr;
          SPA_POD_ARRAY_FOREACH(arr, iter) {
            props.channels.push_back(*reinterpret_cast<const int*>(iter));
          }
        }

        if (muteProp != nullptr) {
          spa_pod_get_bool(&muteProp->value, &props.mute);
        }

        if (volumeStepProp != nullptr) {
          spa_pod_get_float(&volumeStepProp->value, &props.volumeStep);
        }

        return props;
      }
    };

    // As Quickshell connection.cpp:resolveRuntimeDir
    std::string resolveRuntimeDir() {
      const char* pwDir = std::getenv("PIPEWIRE_RUNTIME_DIR");
      if (pwDir != nullptr && *pwDir != '\0') {
        return pwDir;
      }
      const char* xdgDir = std::getenv("XDG_RUNTIME_DIR");
      if (xdgDir != nullptr && *xdgDir != '\0') {
        return xdgDir;
      }
      return "/run/user/" + std::to_string(::getuid());
    }
  } // namespace

  // Forward declarations for internal device management
  class PwDevice;

  struct PwNode::Impl {
    spa_hook listener{};
    PwDevice* device = nullptr;
    int routeDevice = -1;
    bool proAudio = false;
    Connection deviceConn;

    Impl() {
      spa_zero(listener);
    }
  };

  // ── PwDevice (as Quickshell device.hpp / device.cpp) ───────────────────────

  class PwDevice {
  public:
    uint32_t id = 0;
    pw_device* proxy = nullptr;
    int refCount = 0;
    spa_hook listener{};

    Pipewire::Backend* backend = nullptr;

    std::unordered_map<int, int> routeDeviceIndexes;       // routeDevice -> routeIndex
    std::unordered_map<int, PwVolumeProps> routeDeviceVolumes; // routeDevice -> volumeProps
    Signal<int, const PwVolumeProps&> routeVolumesChanged; // (routeDevice, props)

    PwDevice() {
      spa_zero(listener);
    }

    ~PwDevice() {
      unbind();
    }

    void ref();
    void unref();
    void bind();
    void unbind();

    [[nodiscard]] bool hasRouteDevice(int devId) const {
      return routeDeviceIndexes.contains(devId);
    }

    bool tryLoadVolumeProps(int devId, PwVolumeProps& out) const {
      const auto it = routeDeviceVolumes.find(devId);
      if (it != routeDeviceVolumes.end()) {
        out = it->second;
        return true;
      }
      return false;
    }

    bool setVolumes(int devId, const std::vector<double>& vols);
    bool setMuted(int devId, bool mute);

    void onInfo(const pw_device_info* info);
    void onParam(int seq, uint32_t paramId, uint32_t index, uint32_t next, const spa_pod* param);
  };

  // ── PwNodeAudio Interceptors ───────────────────────────────────────────────

  // Interceptors unconditionally forward writes (Finding #0 parity).
  struct PwNodeAudio::MutedInterceptor final : public PropertyInterceptor<bool> {
    PwNodeAudio* self;
    explicit MutedInterceptor(PwNodeAudio* s) : self(s) {}
    bool intercept(const bool& value) override {
      self->setMuted(value);
      return true;
    }
  };

  struct PwNodeAudio::VolumeInterceptor final : public PropertyInterceptor<double> {
    PwNodeAudio* self;
    explicit VolumeInterceptor(PwNodeAudio* s) : self(s) {}
    bool intercept(const double& value) override {
      self->setAverageVolume(value);
      return true;
    }
  };

  struct PwNodeAudio::VolumesInterceptor final : public PropertyInterceptor<std::vector<double>> {
    PwNodeAudio* self;
    explicit VolumesInterceptor(PwNodeAudio* s) : self(s) {}
    bool intercept(const std::vector<double>& value) override {
      self->setVolumes(value);
      return true;
    }
  };

  PwNodeAudio::PwNodeAudio() {
    m_mutedInterceptor = std::make_unique<MutedInterceptor>(this);
    m_volumeInterceptor = std::make_unique<VolumeInterceptor>(this);
    m_volumesInterceptor = std::make_unique<VolumesInterceptor>(this);

    muted.setInterceptor(m_mutedInterceptor.get());
    volume.setInterceptor(m_volumeInterceptor.get());
    volumes.setInterceptor(m_volumesInterceptor.get());

    muted.writeDirect(false);
    volume.writeDirect(0.0);
    channels.writeDirect({});
    volumes.writeDirect({});
  }

  PwNodeAudio::~PwNodeAudio() {
    destroyOwned();
  }

  void PwNodeAudio::init(PwNode* node) {
    m_node = node;
  }

  double PwNodeAudio::averageVolume() const noexcept {
    const auto& v = volumes.peek();
    if (v.empty()) {
      return 0.0;
    }
    double sum = 0.0;
    for (const double val : v) {
      sum += val;
    }
    return sum / static_cast<double>(v.size());
  }

  // As Quickshell node.cpp:setAverageVolume
  void PwNodeAudio::setAverageVolume(double val) {
    const auto& current = volumes.peek();
    // Re-Review Finding #3: No-op when volumes is empty
    if (current.empty()) {
      return;
    }
    const double oldAvg = averageVolume();
    const double mul = (oldAvg <= 1e-6) ? 0.0 : val / oldAvg;
    std::vector<double> target;
    target.reserve(current.size());
    for (const double oldVol : current) {
      target.push_back(mul == 0.0 ? val : oldVol * mul);
    }
    setVolumes(target);
  }

  // As Quickshell node.cpp:setVolumes
  void PwNodeAudio::setVolumes(const std::vector<double>& vols) {
    // Re-Review Finding #3: Return early when node proxy is null regardless of device
    if (m_node != nullptr && m_node->proxy() == nullptr) {
      kLog.warn("Tried to change node volumes for unbound node");
      return;
    }

    std::vector<double> realVolumes;
    realVolumes.reserve(vols.size());
    for (const double v : vols) {
      realVolumes.push_back(std::max(0.0, v));
    }

    if (realVolumes == volumes.peek()) {
      return;
    }

    // Re-Review Finding #3: Unconditional length comparison against current volumes (node.cpp:475)
    if (realVolumes.size() != volumes.peek().size()) {
      kLog.warn("Tried to change node volumes with different length ({}) than channels ({})",
                realVolumes.size(), volumes.peek().size());
      return;
    }

    if (m_node != nullptr) {
      if (m_node->shouldUseDevice()) {
        m_node->setDeviceVolumes(realVolumes);
      } else if (m_node->proxy() != nullptr) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
        std::array<std::uint8_t, 1024> buffer{};
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer.data(), static_cast<uint32_t>(buffer.size()));

        std::vector<float> cubedVolumes;
        cubedVolumes.reserve(realVolumes.size());
        for (const double v : realVolumes) {
          cubedVolumes.push_back(Pipewire::visualToLinear(v));
        }

        auto* pod = spa_pod_builder_add_object(
          &b, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
          SPA_PROP_channelVolumes,
          SPA_POD_Array(sizeof(float), SPA_TYPE_Float, static_cast<uint32_t>(cubedVolumes.size()), cubedVolumes.data())
        );

        pw_node_set_param(m_node->proxy(), SPA_PARAM_Props, 0, static_cast<const spa_pod*>(pod));
#pragma GCC diagnostic pop
      }
    }

    // Write volume before volumes so volumes.changed() sees the updated average
    double avg = 0.0;
    if (!realVolumes.empty()) {
      double sum = 0.0;
      for (const double v : realVolumes) {
        sum += v;
      }
      avg = sum / static_cast<double>(realVolumes.size());
    }
    volume.writeDirect(avg);
    volumes.writeDirect(realVolumes);
  }

  // As Quickshell node.cpp:setMuted
  void PwNodeAudio::setMuted(bool mute) {
    // Re-Review Finding #3: Return early when node proxy is null regardless of device
    if (m_node != nullptr && m_node->proxy() == nullptr) {
      kLog.warn("Tried to change mute state for unbound node");
      return;
    }

    if (mute == muted.peek()) {
      return;
    }

    if (m_node != nullptr) {
      if (m_node->shouldUseDevice()) {
        m_node->setDeviceMuted(mute);
      } else if (m_node->proxy() != nullptr) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
        std::array<std::uint8_t, 1024> buffer{};
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer.data(), static_cast<uint32_t>(buffer.size()));

        auto* pod = spa_pod_builder_add_object(
          &b, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
          SPA_PROP_mute, SPA_POD_Bool(mute)
        );

        pw_node_set_param(m_node->proxy(), SPA_PARAM_Props, 0, static_cast<const spa_pod*>(pod));
#pragma GCC diagnostic pop
      }
    }

    muted.writeDirect(mute);
  }

  // Channels -> volume -> volumes -> muted emit order
  void PwNodeAudio::updateFromServer(bool mute, const std::vector<int>& ch, const std::vector<double>& vols) {
    if (channels.peek() != ch) {
      channels.writeDirect(ch);
    }
    double avg = 0.0;
    if (!vols.empty()) {
      double sum = 0.0;
      for (const double v : vols) {
        sum += v;
      }
      avg = sum / static_cast<double>(vols.size());
    }
    volume.writeDirect(avg);
    if (volumes.peek() != vols) {
      volumes.writeDirect(vols);
    }
    if (muted.peek() != mute) {
      muted.writeDirect(mute);
    }
  }

  // ── PwNode ─────────────────────────────────────────────────────────────────

  PwNode::PwNode() : m_impl(std::make_unique<Impl>()) {
    id.writeDirect(0);
    name.writeDirect("");
    description.writeDirect("");
    nickname.writeDirect("");
    isSink.writeDirect(false);
    isStream.writeDirect(false);
    type.writeDirect(PwNodeType::Untracked);
    properties.writeDirect(js::Json::object());
    ready.writeDirect(false);
  }

  PwNode::~PwNode() {
    destroyOwned();
    destroying.emit(this);
    unbind();
  }

  // ── Pipewire::Backend ──────────────────────────────────────────────────────

  struct Pipewire::Backend {
    Pipewire* pipewire = nullptr;

    pw_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;

    pw_metadata* defaultMetadata = nullptr;
    uint32_t defaultMetadataId = 0;
    uint32_t defaultMetadataPerms = 0;

    spa_hook coreListener{};
    spa_hook registryListener{};
    spa_hook metadataListener{};

    FdWatch::Id watchId = 0;
    int coreSyncSeq = 0;

    enum class InitState : uint8_t { SendingObjects, Binding, Done } initState = InitState::SendingObjects;

    std::unordered_map<uint32_t, PwNode*> nodesById;
    std::unordered_map<uint32_t, std::unique_ptr<PwDevice>> devicesById;
    std::string defaultSinkName;
    std::string defaultSourceName;
    std::string defaultConfiguredSinkName;
    std::string defaultConfiguredSourceName;

    bool fatalErrorQueued = false;

    // Reconnection & socket watcher (Re-Review Finding #1)
    std::string runtimeDir;
    int inotifyFd = -1;
    int inotifyWd = -1;
    FdWatch::Id inotifyWatchId = 0;

    static Backend* s_instance;

    explicit Backend(Pipewire* pw) : pipewire(pw) {
      s_instance = this;
      spa_zero(coreListener);
      spa_zero(registryListener);
      spa_zero(metadataListener);
      runtimeDir = resolveRuntimeDir();
      init();
    }

    ~Backend() {
      stopSocketWatcher();
      shutdown();
      if (s_instance == this) {
        s_instance = nullptr;
      }
    }

    bool tryConnect(bool retry) {
      if (core != nullptr) {
        return true;
      }

      pw_init(nullptr, nullptr);

      loop = pw_loop_new(nullptr);
      if (loop == nullptr) {
        if (!retry) {
          kLog.warn("Failed to create pipewire event loop");
        }
        return false;
      }

      context = pw_context_new(loop, nullptr, 0);
      if (context == nullptr) {
        if (!retry) {
          kLog.warn("Failed to create pipewire context");
        }
        shutdown();
        return false;
      }

      core = pw_context_connect(context, nullptr, 0);
      if (core == nullptr) {
        if (!retry) {
          kLog.info("PipeWire daemon not reachable (errno {})", errno);
        }
        shutdown();
        return false;
      }

      static const pw_core_events kCoreEvents = {
        .version = PW_VERSION_CORE_EVENTS,
        .info = nullptr,
        .done = [](void* data, uint32_t id, int seq) {
          static_cast<Backend*>(data)->onCoreDone(id, seq);
        },
        .ping = nullptr,
        .error = [](void* data, uint32_t id, int seq, int res, const char* msg) {
          static_cast<Backend*>(data)->onCoreError(id, seq, res, msg);
        },
        .remove_id = nullptr,
        .bound_id = nullptr,
        .add_mem = nullptr,
        .remove_mem = nullptr,
        .bound_props = nullptr,
      };
      pw_core_add_listener(core, &coreListener, &kCoreEvents, this);

      // Driven from FdWatch via pw_loop_get_fd + pw_loop_iterate(loop, 0)
      const int fd = pw_loop_get_fd(loop);
      watchId = FdWatch::watch(fd, POLLIN, [this](short) { poll(); });

      registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
      static const pw_registry_events kRegistryEvents = {
        .version = PW_VERSION_REGISTRY_EVENTS,
        .global = [](void* data, uint32_t id, uint32_t permissions, const char* type, uint32_t version, const spa_dict* props) {
          static_cast<Backend*>(data)->onGlobal(id, permissions, type, version, props);
        },
        .global_remove = [](void* data, uint32_t id) {
          static_cast<Backend*>(data)->onGlobalRemove(id);
        },
      };
      pw_registry_add_listener(registry, &registryListener, &kRegistryEvents, this);

      coreSyncSeq = pw_core_sync(core, PW_ID_CORE, 0);
      initState = InitState::SendingObjects;
      poll();

      stopSocketWatcher();
      return true;
    }

    void init() {
      if (!tryConnect(false)) {
        beginReconnect();
      }
    }

    // Re-Review Finding #1: Port Quickshell connection.cpp:beginReconnect / startSocketWatcher
    void beginReconnect() {
      if (core != nullptr) {
        stopSocketWatcher();
        return;
      }

      if (runtimeDir.empty()) {
        kLog.warn("Cannot watch runtime dir for pipewire reconnects: runtime dir is empty.");
        return;
      }

      startSocketWatcher();
      tryConnect(true);
    }

    void startSocketWatcher() {
      if (inotifyFd >= 0) {
        return;
      }

      inotifyFd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
      if (inotifyFd < 0) {
        return;
      }

      inotifyWd = inotify_add_watch(inotifyFd, runtimeDir.c_str(), IN_CREATE | IN_MOVED_TO | IN_ATTRIB);
      if (inotifyWd < 0) {
        ::close(inotifyFd);
        inotifyFd = -1;
        return;
      }

      inotifyWatchId = FdWatch::watch(inotifyFd, POLLIN, [this](short) {
        onRuntimeDirChanged();
      });
    }

    void stopSocketWatcher() {
      if (inotifyWatchId != 0) {
        FdWatch::unwatch(inotifyWatchId);
        inotifyWatchId = 0;
      }
      if (inotifyWd >= 0) {
        inotify_rm_watch(inotifyFd, inotifyWd);
        inotifyWd = -1;
      }
      if (inotifyFd >= 0) {
        ::close(inotifyFd);
        inotifyFd = -1;
      }
    }

    void onRuntimeDirChanged() {
      if (inotifyFd >= 0) {
        char buf[4096];
        while (::read(inotifyFd, buf, sizeof(buf)) > 0) {}
      }

      if (core != nullptr) {
        stopSocketWatcher();
        return;
      }

      tryConnect(true);
    }

    void shutdown() {
      if (watchId != 0) {
        FdWatch::unwatch(watchId);
        watchId = 0;
      }

      if (defaultMetadata != nullptr) {
        spa_hook_remove(&metadataListener);
        spa_zero(metadataListener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(defaultMetadata));
        defaultMetadata = nullptr;
      }

      for (auto& [_, node] : nodesById) {
        node->unbind();
      }
      nodesById.clear();
      devicesById.clear();

      if (registry != nullptr) {
        spa_hook_remove(&registryListener);
        spa_zero(registryListener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
        registry = nullptr;
      }

      if (core != nullptr) {
        spa_hook_remove(&coreListener);
        spa_zero(coreListener);
        pw_core_disconnect(core);
        core = nullptr;
      }

      if (context != nullptr) {
        pw_context_destroy(context);
        context = nullptr;
      }

      if (loop != nullptr) {
        pw_loop_destroy(loop);
        loop = nullptr;
      }

      if (pipewire != nullptr) {
        pipewire->ready.writeDirect(false);
      }
    }

    void poll() {
      if (loop != nullptr) {
        pw_loop_iterate(loop, 0);
      }
    }

    // Re-Review Finding #1: onFatalError resets state and reconnects (connection.cpp:116-124)
    void queueFatalError() {
      if (fatalErrorQueued) {
        return;
      }
      fatalErrorQueued = true;
      DeferredCall::callLater([this] {
        fatalErrorQueued = false;
        onFatalError();
      });
    }

    void onFatalError() {
      kLog.info("Resetting PipeWire state on fatal error and reconnecting");
      defaultSinkName.clear();
      defaultSourceName.clear();
      defaultConfiguredSinkName.clear();
      defaultConfiguredSourceName.clear();

      if (pipewire != nullptr) {
        pipewire->defaultAudioSink.writeDirect(nullptr);
        pipewire->defaultAudioSource.writeDirect(nullptr);
        pipewire->preferredDefaultAudioSink.writeDirect(nullptr);
        pipewire->preferredDefaultAudioSource.writeDirect(nullptr);

        for (auto& [_, node] : nodesById) {
          pipewire->nodes.peek()->removeObject(node);
          node->unbind();
          node->deleteLater();
        }
      }

      nodesById.clear();
      devicesById.clear();

      initState = InitState::SendingObjects;
      coreSyncSeq = 0;
      defaultMetadataId = 0;

      shutdown();
      beginReconnect();
    }

    // As Quickshell core.cpp:onSync / registry.cpp:onCoreSync
    void onCoreDone(uint32_t id, int seq) {
      if (id == PW_ID_CORE && seq == coreSyncSeq) {
        switch (initState) {
          case InitState::SendingObjects:
            coreSyncSeq = pw_core_sync(core, PW_ID_CORE, 0);
            initState = InitState::Binding;
            break;
          case InitState::Binding:
            initState = InitState::Done;
            pipewire->ready.writeDirect(true);
            break;
          default:
            break;
        }
      } else {
        auto it = nodesById.find(id);
        if (it != nodesById.end()) {
          it->second->onSyncDone(seq);
        }
      }
    }

    // As Quickshell core.cpp:onError
    void onCoreError(uint32_t id, int /*seq*/, int res, const char* msg) {
      if (res == -ENOENT) {
        return;
      }
      kLog.warn("Pipewire error on object {} code {}: {}", id, res, msg != nullptr ? msg : "");
      queueFatalError();
    }

    // As Quickshell registry.cpp:onGlobal
    void onGlobal(uint32_t id, uint32_t permissions, const char* type, uint32_t /*version*/, const spa_dict* props) {
      if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
        const char* metaName = props != nullptr ? spa_dict_lookup(props, PW_KEY_METADATA_NAME) : nullptr;
        if (metaName != nullptr && std::strcmp(metaName, "default") == 0) {
          bindDefaultMetadata(id, permissions);
        }
      } else if (std::strcmp(type, PW_TYPE_INTERFACE_Device) == 0) {
        auto dev = std::make_unique<PwDevice>();
        dev->id = id;
        dev->backend = this;
        devicesById[id] = std::move(dev);
      } else if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        addNode(id, permissions, props);
      }
    }

    // As Quickshell defaults.cpp:onMetadataAdded
    void bindDefaultMetadata(uint32_t id, uint32_t permissions) {
      if (defaultMetadata != nullptr) {
        spa_hook_remove(&metadataListener);
        spa_zero(metadataListener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(defaultMetadata));
        defaultMetadata = nullptr;
      }

      defaultMetadataId = id;
      defaultMetadataPerms = permissions;
      defaultMetadata = static_cast<pw_metadata*>(
        pw_registry_bind(registry, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0)
      );
      if (defaultMetadata == nullptr) {
        return;
      }

      static const pw_metadata_events kMetaEvents = {
        .version = PW_VERSION_METADATA_EVENTS,
        .property = [](void* data, uint32_t subject, const char* key, const char* type, const char* value) -> int {
          static_cast<Backend*>(data)->onMetadataProperty(subject, key, type, value);
          return 0;
        },
      };
      pw_metadata_add_listener(defaultMetadata, &metadataListener, &kMetaEvents, this);
    }

    // As Quickshell defaults.cpp:onMetadataProperty
    void onMetadataProperty(uint32_t /*subject*/, const char* key, const char* type, const char* value) {
      if (key == nullptr) {
        return;
      }

      std::string name;
      if (type != nullptr && value != nullptr && std::strcmp(type, "Spa:String:JSON") == 0) {
        try {
          const auto j = nlohmann::json::parse(value);
          if (j.is_object() && j.contains("name") && j["name"].is_string()) {
            name = j["name"].get<std::string>();
          }
        } catch (...) {}
      }

      if (std::strcmp(key, "default.audio.sink") == 0) {
        defaultSinkName = name;
        PwNode* node = pipewire->findNodeByName(name);
        pipewire->defaultAudioSink.writeDirect(node);
      } else if (std::strcmp(key, "default.audio.source") == 0) {
        defaultSourceName = name;
        PwNode* node = pipewire->findNodeByName(name);
        pipewire->defaultAudioSource.writeDirect(node);
      } else if (std::strcmp(key, "default.configured.audio.sink") == 0) {
        defaultConfiguredSinkName = name;
        PwNode* node = pipewire->findNodeByName(name);
        pipewire->preferredDefaultAudioSink.writeDirect(node);
      } else if (std::strcmp(key, "default.configured.audio.source") == 0) {
        defaultConfiguredSourceName = name;
        PwNode* node = pipewire->findNodeByName(name);
        pipewire->preferredDefaultAudioSource.writeDirect(node);
      }
    }

    // As Quickshell defaults.cpp:setConfiguredDefault
    bool setConfiguredDefault(const char* key, const std::string& value) {
      if (defaultMetadata == nullptr) {
        kLog.warn("Cannot set default node as metadata is not ready.");
        return false;
      }
      if ((defaultMetadataPerms & (PW_PERM_W | PW_PERM_X)) != (PW_PERM_W | PW_PERM_X)) {
        kLog.warn("Cannot set default node as write+execute permissions are missing.");
        return false;
      }
      if (value.empty()) {
        pw_metadata_set_property(defaultMetadata, PW_ID_CORE, key, "Spa:String:JSON", nullptr);
      } else {
        const std::string jsonStr = nlohmann::json{{"name", value}}.dump();
        pw_metadata_set_property(defaultMetadata, PW_ID_CORE, key, "Spa:String:JSON", jsonStr.c_str());
      }
      return true;
    }

    // As Quickshell node.cpp:initProps / registry.cpp:onGlobal
    void addNode(uint32_t id, uint32_t /*permissions*/, const spa_dict* props) {
      PwNode* node = pipewire->create<PwNode>();
      node->id.writeDirect(static_cast<int>(id));

      int nodeType = PwNodeType::Untracked;
      if (props != nullptr) {
        if (const char* mediaClass = spa_dict_lookup(props, SPA_KEY_MEDIA_CLASS)) {
          if (std::strcmp(mediaClass, "Audio/Sink") == 0) {
            nodeType = PwNodeType::AudioSink;
          } else if (std::strcmp(mediaClass, "Audio/Source") == 0) {
            nodeType = PwNodeType::AudioSource;
          } else if (std::strcmp(mediaClass, "Audio/Duplex") == 0) {
            nodeType = PwNodeType::AudioDuplex;
          } else if (std::strcmp(mediaClass, "Stream/Output/Audio") == 0) {
            nodeType = PwNodeType::AudioOutStream;
          } else if (std::strcmp(mediaClass, "Stream/Input/Audio") == 0) {
            nodeType = PwNodeType::AudioInStream;
          } else if (std::strcmp(mediaClass, "Video/Sink") == 0) {
            nodeType = PwNodeType::VideoSink;
          } else if (std::strcmp(mediaClass, "Video/Source") == 0) {
            nodeType = PwNodeType::VideoSource;
          }
        }

        if (const char* nodeName = spa_dict_lookup(props, PW_KEY_NODE_NAME)) {
          node->name.writeDirect(nodeName);
        }
        if (const char* nodeDesc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION)) {
          node->description.writeDirect(nodeDesc);
        }
        if (const char* nodeNick = spa_dict_lookup(props, PW_KEY_NODE_NICK)) {
          node->nickname.writeDirect(nodeNick);
        }

        // Link backing device
        if (const char* devIdStr = spa_dict_lookup(props, PW_KEY_DEVICE_ID)) {
          try {
            const auto devId = static_cast<uint32_t>(std::stoul(devIdStr));
            const auto it = devicesById.find(devId);
            if (it != devicesById.end()) {
              node->impl()->device = it->second.get();
            }
          } catch (...) {}
        }
      }

      node->type.writeDirect(nodeType);
      node->isSink.writeDirect((nodeType & PwNodeType::Sink) != 0);
      node->isStream.writeDirect((nodeType & PwNodeType::Stream) != 0);
      node->ready.writeDirect(false);

      if ((nodeType & PwNodeType::Audio) != 0) {
        PwNodeAudio* audio = node->create<PwNodeAudio>();
        audio->init(node);
        audio->complete();
        node->audio.writeDirect(audio);
      }

      node->complete();

      nodesById[id] = node;
      pipewire->nodes.peek()->insertObject(node);

      // As Quickshell defaults.cpp:onNodeAdded
      const std::string& nname = node->name.peek();
      if (!nname.empty()) {
        if (pipewire->defaultAudioSink.peek() == nullptr && nname == defaultSinkName) {
          pipewire->defaultAudioSink.writeDirect(node);
        }
        if (pipewire->defaultAudioSource.peek() == nullptr && nname == defaultSourceName) {
          pipewire->defaultAudioSource.writeDirect(node);
        }
        if (pipewire->preferredDefaultAudioSink.peek() == nullptr && nname == defaultConfiguredSinkName) {
          pipewire->preferredDefaultAudioSink.writeDirect(node);
        }
        if (pipewire->preferredDefaultAudioSource.peek() == nullptr && nname == defaultConfiguredSourceName) {
          pipewire->preferredDefaultAudioSource.writeDirect(node);
        }
      }
    }

    // Re-Review Finding #2: Quickshell parity: Do NOT remove devices on global_remove!
    // In Quickshell registry.cpp:242-255, onGlobalRemoved only removes metadata, links, and nodes.
    // Devices are freed only in reset().
    void onGlobalRemove(uint32_t id) {
      if (id == defaultMetadataId) {
        if (defaultMetadata != nullptr) {
          spa_hook_remove(&metadataListener);
          spa_zero(metadataListener);
          pw_proxy_destroy(reinterpret_cast<pw_proxy*>(defaultMetadata));
          defaultMetadata = nullptr;
        }
        defaultMetadataId = 0;
        return;
      }

      const auto it = nodesById.find(id);
      if (it != nodesById.end()) {
        PwNode* node = it->second;
        nodesById.erase(it);

        if (pipewire->defaultAudioSink.peek() == node) {
          pipewire->defaultAudioSink.writeDirect(nullptr);
        }
        if (pipewire->defaultAudioSource.peek() == node) {
          pipewire->defaultAudioSource.writeDirect(nullptr);
        }
        if (pipewire->preferredDefaultAudioSink.peek() == node) {
          pipewire->preferredDefaultAudioSink.writeDirect(nullptr);
        }
        if (pipewire->preferredDefaultAudioSource.peek() == node) {
          pipewire->preferredDefaultAudioSource.writeDirect(nullptr);
        }

        pipewire->nodes.peek()->removeObject(node);
        node->unbind();
        node->deleteLater();
      }
    }

    // Bind node proxy when refCount becomes > 0 (as Quickshell PwBindableObject::ref / bind)
    void bindNode(PwNode* node) {
      if (registry == nullptr || node->proxy() != nullptr) {
        return;
      }

      if (node->impl()->device != nullptr) {
        node->impl()->device->ref();
        node->impl()->deviceConn = node->impl()->device->routeVolumesChanged.connect(
          [node](int routeDev, const PwVolumeProps& p) {
            if (node->shouldUseDevice() && node->impl()->routeDevice == routeDev) {
              if (PwNodeAudio* a = node->audio.peek()) {
                a->updateFromServer(p.mute, p.channels, p.volumes);
              }
            }
          }
        );
      }

      auto* proxy = static_cast<pw_node*>(
        pw_registry_bind(registry, static_cast<uint32_t>(node->id.peek()), PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0)
      );
      if (proxy == nullptr) {
        return;
      }

      node->setProxy(proxy);

      static const pw_node_events kNodeEvents = {
        .version = PW_VERSION_NODE_EVENTS,
        .info = [](void* data, const pw_node_info* info) {
          static_cast<PwNode*>(data)->onInfo(info);
        },
        .param = [](void* data, int seq, uint32_t paramId, uint32_t index, uint32_t next, const spa_pod* param) {
          static_cast<PwNode*>(data)->onParam(seq, paramId, index, next, param);
        },
      };
      pw_node_add_listener(proxy, &node->impl()->listener, &kNodeEvents, node);
    }

    // Re-Review Finding #3: Reset routeDevice = -1 in unbindNode
    void unbindNode(PwNode* node) {
      if (node->proxy() == nullptr) {
        return;
      }

      node->impl()->deviceConn.disconnect();
      if (node->impl()->device != nullptr) {
        node->impl()->device->unref();
      }
      node->impl()->routeDevice = -1;

      spa_hook_remove(&node->impl()->listener);
      spa_zero(node->impl()->listener);
      pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node->proxy()));
      node->setProxy(nullptr);
      node->ready.writeDirect(false);
      node->onSyncDone(-1);
      node->properties.writeDirect(js::Json::object());

      if (PwNodeAudio* a = node->audio.peek()) {
        a->channels.writeDirect({});
        a->volumes.writeDirect({});
        a->volume.writeDirect(0.0);
      }
    }

    int syncNode(uint32_t nodeId) {
      if (core == nullptr) {
        return 0;
      }
      return pw_core_sync(core, nodeId, 0);
    }
  };

  Pipewire::Backend* Pipewire::Backend::s_instance = nullptr;

  // ── PwDevice Implementation ───────────────────────────────────────────────

  void PwDevice::ref() {
    refCount++;
    if (refCount == 1) {
      bind();
    }
  }

  void PwDevice::unref() {
    if (refCount > 0) {
      refCount--;
      if (refCount == 0) {
        unbind();
      }
    }
  }

  void PwDevice::bind() {
    if (backend == nullptr || backend->registry == nullptr || proxy != nullptr) {
      return;
    }
    proxy = static_cast<pw_device*>(
      pw_registry_bind(backend->registry, id, PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0)
    );
    if (proxy == nullptr) {
      return;
    }

    static const pw_device_events kDevEvents = {
      .version = PW_VERSION_DEVICE_EVENTS,
      .info = [](void* data, const pw_device_info* info) {
        static_cast<PwDevice*>(data)->onInfo(info);
      },
      .param = [](void* data, int seq, uint32_t paramId, uint32_t index, uint32_t next, const spa_pod* param) {
        static_cast<PwDevice*>(data)->onParam(seq, paramId, index, next, param);
      },
    };
    pw_device_add_listener(proxy, &listener, &kDevEvents, this);
  }

  void PwDevice::unbind() {
    if (proxy != nullptr) {
      spa_hook_remove(&listener);
      spa_zero(listener);
      pw_proxy_destroy(reinterpret_cast<pw_proxy*>(proxy));
      proxy = nullptr;
    }
    routeDeviceIndexes.clear();
    routeDeviceVolumes.clear();
  }

  void PwDevice::onInfo(const pw_device_info* info) {
    if (info == nullptr) {
      return;
    }
    if ((info->change_mask & PW_DEVICE_CHANGE_MASK_PARAMS) != 0 && proxy != nullptr) {
      for (uint32_t i = 0; i < info->n_params; ++i) {
        if (info->params[i].id == SPA_PARAM_Route) {
          pw_device_enum_params(proxy, 0, SPA_PARAM_Route, 0, UINT32_MAX, nullptr);
          break;
        }
      }
    }
  }

  void PwDevice::onParam(int /*seq*/, uint32_t paramId, uint32_t /*index*/, uint32_t /*next*/, const spa_pod* param) {
    if (paramId != SPA_PARAM_Route || param == nullptr) {
      return;
    }

    spa_pod_parser parser;
    spa_pod_parser_pod(&parser, param);

    int32_t routeDev = 0;
    int32_t routeIdx = 0;
    const spa_pod* props = nullptr;
    uint32_t routeId = SPA_PARAM_Route;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    spa_pod_parser_get_object(
      &parser, SPA_TYPE_OBJECT_ParamRoute, &routeId,
      SPA_PARAM_ROUTE_device, SPA_POD_Int(&routeDev),
      SPA_PARAM_ROUTE_index, SPA_POD_Int(&routeIdx),
      SPA_PARAM_ROUTE_props, SPA_POD_PodObject(&props)
    );
#pragma GCC diagnostic pop

    PwVolumeProps volumeProps = PwVolumeProps::parseSpaPod(props);
    routeDeviceIndexes[routeDev] = routeIdx;
    routeDeviceVolumes[routeDev] = volumeProps;

    routeVolumesChanged.emit(routeDev, volumeProps);
  }

  bool PwDevice::setVolumes(int devId, const std::vector<double>& vols) {
    if (proxy == nullptr) {
      kLog.warn("Tried to change device route props for unbound device");
      return false;
    }
    const auto it = routeDeviceIndexes.find(devId);
    if (it == routeDeviceIndexes.end()) {
      kLog.warn("Untracked route device {}", devId);
      return false;
    }
    const int routeIndex = it->second;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    std::array<std::uint8_t, 1024> buffer{};
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer.data(), static_cast<uint32_t>(buffer.size()));

    std::vector<float> cubed;
    cubed.reserve(vols.size());
    for (const double v : vols) {
      cubed.push_back(Pipewire::visualToLinear(v));
    }

    auto* props = spa_pod_builder_add_object(
      &b, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
      SPA_PROP_channelVolumes,
      SPA_POD_Array(sizeof(float), SPA_TYPE_Float, static_cast<uint32_t>(cubed.size()), cubed.data())
    );

    auto* route = spa_pod_builder_add_object(
      &b, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route,
      SPA_PARAM_ROUTE_device, SPA_POD_Int(devId),
      SPA_PARAM_ROUTE_index, SPA_POD_Int(routeIndex),
      SPA_PARAM_ROUTE_props, SPA_POD_PodObject(props),
      SPA_PARAM_ROUTE_save, SPA_POD_Bool(true)
    );

    pw_device_set_param(proxy, SPA_PARAM_Route, 0, static_cast<const spa_pod*>(route));
#pragma GCC diagnostic pop
    return true;
  }

  bool PwDevice::setMuted(int devId, bool mute) {
    if (proxy == nullptr) {
      kLog.warn("Tried to change device route props for unbound device");
      return false;
    }
    const auto it = routeDeviceIndexes.find(devId);
    if (it == routeDeviceIndexes.end()) {
      kLog.warn("Untracked route device {}", devId);
      return false;
    }
    const int routeIndex = it->second;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    std::array<std::uint8_t, 1024> buffer{};
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer.data(), static_cast<uint32_t>(buffer.size()));

    auto* props = spa_pod_builder_add_object(
      &b, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
      SPA_PROP_mute, SPA_POD_Bool(mute)
    );

    auto* route = spa_pod_builder_add_object(
      &b, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route,
      SPA_PARAM_ROUTE_device, SPA_POD_Int(devId),
      SPA_PARAM_ROUTE_index, SPA_POD_Int(routeIndex),
      SPA_PARAM_ROUTE_props, SPA_POD_PodObject(props),
      SPA_PARAM_ROUTE_save, SPA_POD_Bool(true)
    );

    pw_device_set_param(proxy, SPA_PARAM_Route, 0, static_cast<const spa_pod*>(route));
#pragma GCC diagnostic pop
    return true;
  }

  // ── PwNode Binding & Events ────────────────────────────────────────────────

  void PwNode::ref() {
    m_refCount++;
    if (m_refCount == 1) {
      bind();
    }
  }

  void PwNode::unref() {
    if (m_refCount > 0) {
      m_refCount--;
      if (m_refCount == 0) {
        unbind();
      }
    }
  }

  void PwNode::bind() {
    if (Pipewire::Backend::s_instance != nullptr) {
      Pipewire::Backend::s_instance->bindNode(this);
    }
  }

  void PwNode::unbind() {
    if (Pipewire::Backend::s_instance != nullptr) {
      Pipewire::Backend::s_instance->unbindNode(this);
    } else if (m_proxy != nullptr) {
      spa_hook_remove(&m_impl->listener);
      spa_zero(m_impl->listener);
      pw_proxy_destroy(reinterpret_cast<pw_proxy*>(m_proxy));
      m_proxy = nullptr;
      ready.writeDirect(false);
      m_syncSeq = 0;
      m_impl->routeDevice = -1;
    }
  }

  // Finding #6: shouldUseDevice
  bool PwNode::shouldUseDevice() const noexcept {
    if (m_impl->device == nullptr || m_impl->proAudio || m_impl->routeDevice == -1) {
      return false;
    }
    return m_impl->device->hasRouteDevice(m_impl->routeDevice);
  }

  void PwNode::setDeviceVolumes(const std::vector<double>& vols) {
    if (m_impl->device != nullptr && m_impl->routeDevice != -1) {
      m_impl->device->setVolumes(m_impl->routeDevice, vols);
    }
  }

  void PwNode::setDeviceMuted(bool mute) {
    if (m_impl->device != nullptr && m_impl->routeDevice != -1) {
      m_impl->device->setMuted(m_impl->routeDevice, mute);
    }
  }

  // As Quickshell node.cpp:onInfo
  void PwNode::onInfo(const void* rawInfo) {
    const auto* info = static_cast<const pw_node_info*>(rawInfo);
    if (info == nullptr) {
      return;
    }

    if ((info->change_mask & PW_NODE_CHANGE_MASK_PROPS) != 0 && info->props != nullptr) {
      if (const char* proAudioStr = spa_dict_lookup(info->props, "device.profile.pro")) {
        m_impl->proAudio = (std::strcmp(proAudioStr, "true") == 0 || std::strcmp(proAudioStr, "1") == 0);
      }

      if (m_impl->device != nullptr) {
        if (const char* routeDeviceStr = spa_dict_lookup(info->props, "card.profile.device")) {
          try {
            const int idVal = std::stoi(routeDeviceStr);
            m_impl->routeDevice = idVal;
            PwVolumeProps initialProps;
            if (m_impl->device->tryLoadVolumeProps(idVal, initialProps)) {
              if (PwNodeAudio* a = audio.peek()) {
                a->updateFromServer(initialProps.mute, initialProps.channels, initialProps.volumes);
              }
            }
          } catch (...) {}
        }
      }

      // Build fresh properties object from bound info
      js::Json jsonProps = js::Json::object();
      const spa_dict_item* item = nullptr;
      spa_dict_for_each(item, info->props) {
        if (item->key != nullptr && item->value != nullptr) {
          jsonProps[item->key] = item->value;
        }
      }
      properties.writeDirect(jsonProps);
    }

    if ((info->change_mask & PW_NODE_CHANGE_MASK_PARAMS) != 0 && m_proxy != nullptr) {
      for (uint32_t i = 0; i < info->n_params; ++i) {
        const auto& p = info->params[i];
        if (p.id == SPA_PARAM_Props) {
          pw_node_enum_params(m_proxy, 0, SPA_PARAM_Props, 0, UINT32_MAX, nullptr);
        }
      }
    }

    if (!ready.peek() && m_syncSeq == 0 && Pipewire::Backend::s_instance != nullptr) {
      m_syncSeq = Pipewire::Backend::s_instance->syncNode(static_cast<uint32_t>(id.peek()));
    }
  }

  // As Quickshell node.cpp:onCoreSync
  void PwNode::onSyncDone(int seq) {
    if (seq == -1) {
      m_syncSeq = 0;
      return;
    }
    if (seq == m_syncSeq) {
      m_syncSeq = 0;
      ready.writeDirect(true);
    }
  }

  // As Quickshell node.cpp:onParam / parseSpaPod
  void PwNode::onParam(int /*seq*/, uint32_t paramId, uint32_t index, uint32_t /*next*/, const void* rawParam) {
    const auto* param = static_cast<const spa_pod*>(rawParam);
    if (paramId == SPA_PARAM_Props && index == 0 && param != nullptr) {
      // Skip node Props updates when using device route
      if (shouldUseDevice()) {
        return;
      }

      PwNodeAudio* a = audio.peek();
      if (a == nullptr) {
        return;
      }

      const PwVolumeProps p = PwVolumeProps::parseSpaPod(param);
      a->updateFromServer(p.mute, p.channels, p.volumes);
    }
  }

  // ── Pipewire Singleton ─────────────────────────────────────────────────────

  struct Pipewire::PreferredSinkInterceptor final : public PropertyInterceptor<PwNode*> {
    Pipewire* self;
    explicit PreferredSinkInterceptor(Pipewire* s) : self(s) {}
    bool intercept(PwNode* const& node) override {
      self->changeConfiguredSink(node);
      return true;
    }
  };

  struct Pipewire::PreferredSourceInterceptor final : public PropertyInterceptor<PwNode*> {
    Pipewire* self;
    explicit PreferredSourceInterceptor(Pipewire* s) : self(s) {}
    bool intercept(PwNode* const& node) override {
      self->changeConfiguredSource(node);
      return true;
    }
  };

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

    m_sinkInterceptor = std::make_unique<PreferredSinkInterceptor>(this);
    m_sourceInterceptor = std::make_unique<PreferredSourceInterceptor>(this);
    preferredDefaultAudioSink.setInterceptor(m_sinkInterceptor.get());
    preferredDefaultAudioSource.setInterceptor(m_sourceInterceptor.get());

    ready.writeDirect(false);
    defaultAudioSink.writeDirect(nullptr);
    defaultAudioSource.writeDirect(nullptr);
    preferredDefaultAudioSink.writeDirect(nullptr);
    preferredDefaultAudioSource.writeDirect(nullptr);

    m_backend = std::make_unique<Backend>(this);
  }

  Pipewire::~Pipewire() {
    destroyOwned();
  }

  PwNode* Pipewire::findNodeByName(std::string_view name) const {
    if (name.empty()) {
      return nullptr;
    }
    const auto& list = nodes.peek()->values.peek();
    for (Object* obj : list) {
      if (auto* node = dynamic_cast<PwNode*>(obj)) {
        if (node->name.peek() == name) {
          return node;
        }
      }
    }
    return nullptr;
  }

  PwNode* Pipewire::findNodeById(int id) const {
    const auto& list = nodes.peek()->values.peek();
    for (Object* obj : list) {
      if (auto* node = dynamic_cast<PwNode*>(obj)) {
        if (node->id.peek() == id) {
          return node;
        }
      }
    }
    return nullptr;
  }

  void Pipewire::changeConfiguredSink(PwNode* node) {
    if (node != nullptr) {
      if ((node->type.peek() & PwNodeType::AudioSink) != PwNodeType::AudioSink) {
        kLog.warn("Cannot change default sink to node that is not an AudioSink");
        return;
      }
      if (m_backend != nullptr) {
        m_backend->setConfiguredDefault("default.configured.audio.sink", node->name.peek());
      }
    } else {
      if (m_backend != nullptr) {
        m_backend->setConfiguredDefault("default.configured.audio.sink", "");
      }
    }
  }

  void Pipewire::changeConfiguredSource(PwNode* node) {
    if (node != nullptr) {
      if ((node->type.peek() & PwNodeType::AudioSource) != PwNodeType::AudioSource) {
        kLog.warn("Cannot change default source to node that is not an AudioSource");
        return;
      }
      if (m_backend != nullptr) {
        m_backend->setConfiguredDefault("default.configured.audio.source", node->name.peek());
      }
    } else {
      if (m_backend != nullptr) {
        m_backend->setConfiguredDefault("default.configured.audio.source", "");
      }
    }
  }

  // Visual/Cubic volume conversions as Quickshell node.cpp line 585
  double Pipewire::linearToVisual(float linear) noexcept {
    return std::cbrt(std::max(0.0f, linear));
  }

  float Pipewire::visualToLinear(double visual) noexcept {
    const float f = std::max(0.0f, static_cast<float>(visual));
    return f * f * f;
  }

  // ── PwObjectTracker ────────────────────────────────────────────────────────

  PwObjectTracker::PwObjectTracker() {
    objects.onChanged([this] { updateTracked(); });
  }

  PwObjectTracker::~PwObjectTracker() {
    m_destroyedConns.clear();
    for (Object* obj : m_tracked) {
      if (auto* node = dynamic_cast<PwNode*>(obj)) {
        node->unref();
      }
    }
    m_tracked.clear();
    destroyOwned();
  }

  void PwObjectTracker::objectDestroyed(Object* obj) {
    std::erase(m_tracked, obj);
    auto cur = objects.peek();
    if (std::erase(cur, obj) > 0) {
      objects.writeDirect(std::move(cur));
    }
  }

  // As Quickshell qml.cpp:setObjects / clearList
  void PwObjectTracker::updateTracked() {
    const auto current = objects.peek();

    // +1 ref before removing old refs to avoid an unbind->bind bounce
    for (Object* obj : current) {
      if (auto* node = dynamic_cast<PwNode*>(obj)) {
        node->ref();
      }
    }

    for (Object* obj : m_tracked) {
      if (auto* node = dynamic_cast<PwNode*>(obj)) {
        node->unref();
      }
    }

    m_destroyedConns.clear();
    for (Object* obj : current) {
      if (obj != nullptr) {
        m_destroyedConns.push_back(obj->destroyed.connect([this, obj] {
          objectDestroyed(obj);
        }));
      }
    }

    m_tracked = current;
  }

} // namespace ii::qs
