// ii-shell, stage 1: shows tests/visual/osd_demo.qml, built with the runtime, centred on every
// output, as an end-to-end check of Item / Rectangle / Text / RowLayout / anchors on screen.
// `--animate` cycles the level with a looping animation (bindings + text + layout per frame).

#include "app/main_loop.h"
#include "core/log.h"
#include "render/gl_shared_context.h"
#include "render/render_context.h"
#include "runtime/animation.h"
#include "runtime/color.h"
#include "runtime/item.h"
#include "runtime/layout.h"
#include "runtime/rectangle.h"
#include "runtime/text.h"
#include "wayland/layer_surface.h"
#include "wayland/wayland_connection.h"

#include <cmath>
#include <csignal>
#include <cstring>
#include <format>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

  constexpr Logger kLog("ii-shell");
  constexpr std::uint32_t kWidth = 300;
  constexpr std::uint32_t kHeight = 72;

  struct Window {
    std::unique_ptr<LayerSurface> surface;
    std::unique_ptr<ii::Item> root;
  };

  // Volume level shown by the demo; animated with --animate.
  ii::Property<double> g_level{0.42};

  void onSignal(int /*signal*/) { MainLoop::requestShutdown(); }

  // Mirrors tests/visual/osd_demo.qml.
  std::unique_ptr<ii::Item> buildOsdDemo(bool animate) {
    using namespace ii;
    auto root = std::make_unique<Item>();
    root->width.set(kWidth);
    root->height.set(kHeight);

    auto* panel = root->add<Rectangle>();
    panel->anchors().fill.set(root.get());
    panel->anchors().margins.set(6);
    panel->radius.set(30);
    panel->color.set(qmlColor("#211f26"));

    auto* row = panel->add<RowLayout>();
    row->anchors().fill.set(panel);
    row->anchors().leftMargin.set(16);
    row->anchors().rightMargin.set(20);
    row->spacing.set(12);

    auto* icon = row->add<Text>();
    icon->text.set("volume_up");
    icon->font.family.set("Material Symbols Rounded");
    icon->font.pixelSize.set(26);
    icon->color.set(qmlColor("#cfbcff"));

    auto* track = row->add<Rectangle>();
    track->layout().fillWidth.set(true);
    track->implicitHeight.set(12);
    track->radius.set(6);
    track->color.set(qmlColor("#4a4458"));
    auto* fill = track->add<Rectangle>();
    fill->width.bind([track] { return track->width.get() * g_level.get(); });
    fill->height.bind([track] { return track->height.get(); });
    fill->radius.set(6);
    fill->color.set(qmlColor("#cfbcff"));

    auto* value = row->add<Text>();
    value->text.bind([] { return std::format("{}", std::lround(g_level.get() * 100.0)); });
    value->font.family.set("Google Sans Flex");
    value->font.pixelSize.set(16);
    value->font.variableAxes.set({{"wght", 450.0}, {"wdth", 100.0}});
    value->color.set(qmlColor("#e6e0e9"));

    if (animate) {
      // 10% -> 90% -> 10%, forever, on ii's expressiveDefaultSpatial curve.
      const Easing curve{.type = Easing::Type::BezierSpline, .bezierCurve = {0.38, 1.21, 0.22, 1.00, 1, 1}};
      auto* cycle = root->create<SequentialAnimation>();
      for (const auto& [from, to] : {std::pair{0.1, 0.9}, std::pair{0.9, 0.1}}) {
        auto* step = cycle->add<NumberAnimation>();
        step->target = &g_level;
        step->from.set(from);
        step->to.set(to);
        step->duration.set(1500);
        step->easing.set(curve);
      }
      cycle->loops.set(Animation::Infinite);
      cycle->running.set(true);
    }
    root->complete();
    return root;
  }

  std::unique_ptr<Window>
  createWindow(WaylandConnection& wayland, RenderContext& render, const WaylandOutput& output, bool animate) {
    auto window = std::make_unique<Window>();
    window->root = buildOsdDemo(animate);
    window->surface = std::make_unique<LayerSurface>(
        wayland,
        LayerSurfaceConfig{
            .nameSpace = "ii-shell:demo",
            .layer = LayerShellLayer::Overlay,
            .width = kWidth,
            .height = kHeight,
            // Centre on the whole output, ignoring other surfaces' exclusive zones (the bar).
            .exclusiveZone = -1,
            .defaultWidth = kWidth,
            .defaultHeight = kHeight,
        }
    );
    window->surface->setRenderContext(&render);
    auto* raw = window.get();
    window->surface->setConfigureCallback([raw](std::uint32_t width, std::uint32_t height) {
      raw->root->width.set(width);
      raw->root->height.set(height);
    });
    // Layout-style work is polished right before the frame is laid out and drawn.
    // Animations advance on frame ticks; their layout effects are polished in the same frame.
    // A tick requested before the first configure is dropped by the surface, so every prepared
    // frame re-requests one while animations run (the first frame after configure prepares).
    auto* surface = window->surface.get();
    window->surface->setPrepareFrameCallback([surface](bool /*needsUpdate*/, bool /*needsLayout*/) {
      ii::flushPolish();
      if (ii::AnimationDriver::instance().hasRunningAnimations()) {
        surface->requestFrameTick();
      }
    });
    window->surface->setFrameTickCallback([surface](float /*deltaMs*/) {
      auto& driver = ii::AnimationDriver::instance();
      driver.tick();
      ii::flushPolish();
      if (driver.hasRunningAnimations()) {
        surface->requestFrameTick();
      }
    });
    window->surface->setSceneRoot(window->root->node());

    if (!window->surface->initialize(output.output)) {
      kLog.warn("failed to create window on output {}", output.connectorName);
      return nullptr;
    }
    return window;
  }

} // namespace

int main(int argc, char** argv) {
  const bool animate = argc > 1 && std::strcmp(argv[1], "--animate") == 0;
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  // Until the shell reads kdeglobals: the application font of the desktop this was built on.
  ii::defaultFont() = ii::FontSpec{"Google Sans Flex", 11.0 * 96.0 / 72.0, 500, false, ""};

  WaylandConnection wayland;
  if (!wayland.connect()) {
    kLog.error("failed to connect to the Wayland display");
    return 1;
  }

  GlSharedContext gl;
  gl.initialize(wayland.display());
  RenderContext render;
  render.initialize(gl);

  std::vector<std::unique_ptr<Window>> windows;
  ii::setPolishRequestHandler([&windows] {
    for (auto& window : windows) {
      window->surface->requestUpdate();
    }
  });
  ii::AnimationDriver::instance().setFrameRequestHandler([&windows] {
    for (auto& window : windows) {
      window->surface->requestFrameTick();
    }
  });
  for (const auto& output : wayland.outputs()) {
    if (auto window = createWindow(wayland, render, output, animate)) {
      // Building the scene queued polish before this window could be asked for a frame.
      if (ii::hasPendingPolish()) {
        window->surface->requestUpdate();
      }
      windows.push_back(std::move(window));
    }
  }
  kLog.info("created {} window(s)", windows.size());

  MainLoop loop(wayland, [] { return std::vector<PollSource*>{}; }, [&windows] {
    ii::setPolishRequestHandler({});
    ii::AnimationDriver::instance().setFrameRequestHandler({});
    windows.clear();
  });
  loop.run();
  return 0;
}
