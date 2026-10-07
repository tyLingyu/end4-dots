#include "compat/pipewire.h"
#include "tests/check.h"
#include "tests/pump.h"

#include <cmath>
#include <string>
#include <vector>

using namespace ii::qs;

TEST("Pipewire cubic volume conversions") {
  // Cubic conversion parity with Quickshell (node.cpp:585, cbrt and cube)
  CHECK(Pipewire::linearToVisual(0.0f) == 0.0);
  CHECK(Pipewire::linearToVisual(1.0f) == 1.0);
  CHECK(std::abs(Pipewire::linearToVisual(0.125f) - 0.5) < 1e-6);

  CHECK(Pipewire::visualToLinear(0.0) == 0.0f);
  CHECK(Pipewire::visualToLinear(1.0) == 1.0f);
  CHECK(std::abs(Pipewire::visualToLinear(0.5) - 0.125f) < 1e-6f);

  for (double visual : {0.0, 0.1, 0.25, 0.5, 0.75, 1.0, 1.2}) {
    float linear = Pipewire::visualToLinear(visual);
    double back = Pipewire::linearToVisual(linear);
    CHECK(std::abs(back - visual) < 1e-5);
  }
}

TEST("PwNodeAudio volume averaging and scaling") {
  PwNodeAudio audio;
  audio.complete();

  CHECK_EQ(audio.volume.get(), 0.0);
  CHECK_EQ(audio.muted.get(), false);
  CHECK(audio.channels.get().empty());
  CHECK(audio.volumes.get().empty());

  // Simulate server sending initial audio properties (stereo, 0.4 and 0.6)
  audio.updateFromServer(false, {PwAudioChannel::FrontLeft, PwAudioChannel::FrontRight}, {0.4, 0.6});

  CHECK_EQ(audio.muted.get(), false);
  CHECK_EQ(audio.channels.get().size(), 2u);
  CHECK_EQ(audio.volumes.get().size(), 2u);
  CHECK(std::abs(audio.volume.get() - 0.5) < 1e-6);

  int volumesChangedCount = 0;
  const auto connVolumes = audio.volumes.changed().connect([&] { ++volumesChangedCount; });

  // Set average volume to 0.75. Channels should scale proportionally:
  // oldAvg = 0.5 -> mul = 0.75 / 0.5 = 1.5.
  // Left: 0.4 * 1.5 = 0.6, Right: 0.6 * 1.5 = 0.9.
  audio.volume.set(0.75);

  CHECK(std::abs(audio.volume.get() - 0.75) < 1e-6);
  CHECK_EQ(audio.volumes.get().size(), 2u);
  CHECK(std::abs(audio.volumes.get()[0] - 0.6) < 1e-6);
  CHECK(std::abs(audio.volumes.get()[1] - 0.9) < 1e-6);
  CHECK_EQ(volumesChangedCount, 1);

  // Set average volume to 0.0
  audio.volume.set(0.0);
  CHECK_EQ(audio.volume.get(), 0.0);
  CHECK_EQ(audio.volumes.get()[0], 0.0);
  CHECK_EQ(audio.volumes.get()[1], 0.0);
  CHECK_EQ(volumesChangedCount, 2);

  // Set average volume from 0.0 to 0.5: each channel should become 0.5
  audio.volume.set(0.5);
  CHECK(std::abs(audio.volume.get() - 0.5) < 1e-6);
  CHECK(std::abs(audio.volumes.get()[0] - 0.5) < 1e-6);
  CHECK(std::abs(audio.volumes.get()[1] - 0.5) < 1e-6);
  CHECK_EQ(volumesChangedCount, 3);

  // Set individual volumes
  audio.volumes.set({0.2, 0.8});
  CHECK(std::abs(audio.volume.get() - 0.5) < 1e-6);
  CHECK(std::abs(audio.volumes.get()[0] - 0.2) < 1e-6);
  CHECK(std::abs(audio.volumes.get()[1] - 0.8) < 1e-6);
  CHECK_EQ(volumesChangedCount, 4);

  // Mute toggle
  int mutedChangedCount = 0;
  const auto connMuted = audio.muted.changed().connect([&] { ++mutedChangedCount; });

  audio.muted.set(true);
  CHECK_EQ(audio.muted.get(), true);
  CHECK_EQ(mutedChangedCount, 1);

  audio.muted.set(false);
  CHECK_EQ(audio.muted.get(), false);
  CHECK_EQ(mutedChangedCount, 2);
}

// Finding #0 & #1: Nested user writes from volumes.changed() handler
// and verification that volume is updated BEFORE volumes.changed() emits.
TEST("PwNodeAudio nested write in volumes.changed (volume protection)") {
  PwNodeAudio audio;
  audio.complete();
  audio.updateFromServer(false, {PwAudioChannel::FrontLeft, PwAudioChannel::FrontRight}, {0.3, 0.3});

  double lastVolume = 0.3;
  std::vector<double> observedVolumes;
  bool reverted = false;

  const auto conn = audio.volumes.changed().connect([&] {
    const double vol = audio.volume.get();
    observedVolumes.push_back(vol);
    // Simulate Audio.cpp volume protection: revert jump greater than 0.1
    if (vol > lastVolume + 0.1) {
      reverted = true;
      audio.volume.set(lastVolume);
    } else {
      lastVolume = vol;
    }
  });

  // Server sends sudden increase to 0.90
  audio.updateFromServer(false, {PwAudioChannel::FrontLeft, PwAudioChannel::FrontRight}, {0.9, 0.9});

  // Handler must have seen the new volume 0.90 first (Finding #1), not stale 0.30
  CHECK_EQ(observedVolumes.size(), 2u);
  CHECK(std::abs(observedVolumes[0] - 0.9) < 1e-6);
  CHECK(std::abs(observedVolumes[1] - 0.3) < 1e-6);
  // Revert must have triggered
  CHECK(reverted);
  // Volume must have been successfully set back to 0.3 (Finding #0: not dropped)
  CHECK(std::abs(audio.volume.get() - 0.3) < 1e-6);
}

// Finding #1: Emit order in updateFromServer: channels -> volumes -> muted
TEST("PwNodeAudio emit order") {
  PwNodeAudio audio;
  audio.complete();

  std::vector<std::string> emitOrder;
  const auto c1 = audio.channels.changed().connect([&] { emitOrder.push_back("channels"); });
  const auto c2 = audio.volumes.changed().connect([&] {
    emitOrder.push_back("volumes");
    // Ensure channels and volumes have the same length when volumes.changed() runs
    CHECK_EQ(audio.channels.get().size(), audio.volumes.get().size());
  });
  const auto c3 = audio.muted.changed().connect([&] { emitOrder.push_back("muted"); });

  audio.updateFromServer(true, {PwAudioChannel::FrontLeft, PwAudioChannel::FrontRight}, {0.5, 0.5});

  CHECK_EQ(emitOrder.size(), 3u);
  CHECK_EQ(emitOrder[0], "channels");
  CHECK_EQ(emitOrder[1], "volumes");
  CHECK_EQ(emitOrder[2], "muted");
}

TEST("PwNodeAudio reactive bindings") {
  PwNodeAudio audio;
  audio.complete();
  audio.updateFromServer(false, {PwAudioChannel::FrontLeft, PwAudioChannel::FrontRight}, {0.5, 0.5});

  ii::Property<double> boundVolume;
  boundVolume.bind([&] { return audio.volume.get(); });

  CHECK(std::abs(boundVolume.get() - 0.5) < 1e-6);

  audio.volume.set(0.8);
  CHECK(std::abs(boundVolume.get() - 0.8) < 1e-6);

  audio.volumes.set({0.1, 0.3});
  CHECK(std::abs(boundVolume.get() - 0.2) < 1e-6);
}

TEST("PwNode types and flags") {
  PwNode sinkNode;
  sinkNode.type.set(PwNodeType::AudioSink);
  sinkNode.isSink.set((sinkNode.type.get() & PwNodeType::Sink) != 0);
  sinkNode.isStream.set((sinkNode.type.get() & PwNodeType::Stream) != 0);

  CHECK_EQ(sinkNode.isSink.get(), true);
  CHECK_EQ(sinkNode.isStream.get(), false);

  PwNode streamNode;
  streamNode.type.set(PwNodeType::AudioOutStream);
  streamNode.isSink.set((streamNode.type.get() & PwNodeType::Sink) != 0);
  streamNode.isStream.set((streamNode.type.get() & PwNodeType::Stream) != 0);

  CHECK_EQ(streamNode.isSink.get(), true);
  CHECK_EQ(streamNode.isStream.get(), true);
}

// Finding #3: PwObjectTracker cleans up destroyed nodes without dangling pointers
TEST("PwObjectTracker reference counting and destruction") {
  PwNode node1;
  PwNode node2;

  CHECK_EQ(node1.refCount(), 0);
  CHECK_EQ(node2.refCount(), 0);

  {
    PwObjectTracker tracker;
    tracker.objects.set({&node1});

    CHECK_EQ(node1.refCount(), 1);
    CHECK_EQ(node2.refCount(), 0);

    // Update tracked set: node1 kept, node2 added (no unbind bounce on node1)
    tracker.objects.set({&node1, &node2});

    CHECK_EQ(node1.refCount(), 1);
    CHECK_EQ(node2.refCount(), 1);

    // Remove node1
    tracker.objects.set({&node2});

    CHECK_EQ(node1.refCount(), 0);
    CHECK_EQ(node2.refCount(), 1);

    // Finding #3: Dynamically created node destroyed while tracked
    auto* node3 = tracker.create<PwNode>();
    tracker.objects.set({&node2, node3});
    CHECK_EQ(node3->refCount(), 1);

    node3->deleteLater();
    ii_test::drainDeferred();

    // node3 was destroyed; tracker's objects must not contain node3
    const auto& objs = tracker.objects.get();
    for (auto* o : objs) {
      CHECK(o != node3);
    }
  }
  // tracker destroyed: node2 unref'd
  CHECK_EQ(node2.refCount(), 0);
}

// Finding #5 & #7: Validation and rejection checks
TEST("Pipewire preferred default audio sink validation") {
  Pipewire& pw = Pipewire::instance();

  PwNode videoNode;
  videoNode.type.set(PwNodeType::VideoSink);
  videoNode.name.set("video_sink");

  // Setting a VideoSink as preferred default audio sink must be rejected
  pw.preferredDefaultAudioSink.set(&videoNode);
  CHECK(pw.preferredDefaultAudioSink.get() == nullptr);
}
TEST("Re-Review: Reject volume/mute writes on unbound node and length mismatch") {
  PwNode unboundNode;
  unboundNode.complete();
  auto* audio = unboundNode.create<PwNodeAudio>();
  audio->init(&unboundNode);
  audio->complete();
  unboundNode.audio.set(audio);

  // Simulate initial parameters from server
  audio->updateFromServer(false, {PwAudioChannel::FrontLeft, PwAudioChannel::FrontRight}, {0.5, 0.5});

  CHECK(std::abs(audio->volume.get() - 0.5) < 1e-6);
  CHECK_EQ(audio->muted.get(), false);

  // 1. Unbound node: volume.set must be rejected! (Quickshell node.cpp:462-466)
  audio->volume.set(0.8);
  CHECK(std::abs(audio->volume.get() - 0.5) < 1e-6);

  // 2. Unbound node: muted.set must be rejected! (Quickshell node.cpp:399-402)
  audio->muted.set(true);
  CHECK_EQ(audio->muted.get(), false);

  // 3. Standalone audio: mismatched length must be rejected! (node.cpp:475)
  PwNodeAudio standaloneAudio;
  standaloneAudio.complete();
  standaloneAudio.updateFromServer(false, {PwAudioChannel::FrontLeft, PwAudioChannel::FrontRight}, {0.4, 0.4});

  standaloneAudio.volumes.set({0.1, 0.2, 0.3}); // 3 channels instead of 2
  CHECK_EQ(standaloneAudio.volumes.get().size(), 2u);
  CHECK(std::abs(standaloneAudio.volume.get() - 0.4) < 1e-6);

  // 4. Standalone audio: setAverageVolume on empty volumes is a no-op
  PwNodeAudio emptyAudio;
  emptyAudio.complete();
  emptyAudio.volume.set(0.7);
  CHECK_EQ(emptyAudio.volume.get(), 0.0);
  CHECK(emptyAudio.volumes.get().empty());
}

TEST_MAIN()
