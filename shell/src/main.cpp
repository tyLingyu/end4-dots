// ii-shell scaffold: one rounded panel with a label on every output.
// Exercises the vendored Wayland + GLES stack end to end until the QML runtime exists.

#include "app/main_loop.h"
#include "core/log.h"
#include "render/core/color.h"
#include "render/gl_shared_context.h"
#include "render/render_context.h"
#include "render/scene/node.h"
#include "render/scene/rect_node.h"
#include "render/scene/text_node.h"
#include "wayland/layer_surface.h"
#include "wayland/wayland_connection.h"

#include <csignal>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

  constexpr Logger kLog("ii-shell");
  constexpr std::uint32_t kPanelHeight = 40;
  constexpr std::int32_t kPanelMargin = 8;

  struct Panel {
    std::unique_ptr<LayerSurface> surface;
    std::unique_ptr<Node> root;
    RectNode* background = nullptr;
    TextNode* label = nullptr;
  };

  void onSignal(int /*signal*/) { MainLoop::requestShutdown(); }

  std::unique_ptr<Panel> createPanel(WaylandConnection& wayland, RenderContext& render, const WaylandOutput& output) {
    auto panel = std::make_unique<Panel>();

    panel->root = std::make_unique<Node>();
    panel->background = static_cast<RectNode*>(panel->root->addChild(std::make_unique<RectNode>()));
    RoundedRectStyle style;
    style.fill = hex("#1d1b20");
    style.radius = Radii(kPanelHeight / 2.0F);
    panel->background->setStyle(style);

    panel->label = static_cast<TextNode*>(panel->root->addChild(std::make_unique<TextNode>()));
    panel->label->setText("ii-shell scaffold");
    panel->label->setFontSize(14.0F);
    panel->label->setColor(hex("#e6e0e9"));
    // TextNode's origin is the text baseline, not the top of the line box.
    panel->label->setPosition(20.0F, 25.0F);

    panel->surface = std::make_unique<LayerSurface>(
        wayland,
        LayerSurfaceConfig{
            .nameSpace = "ii-shell:scaffold",
            .layer = LayerShellLayer::Top,
            .anchor = LayerShellAnchor::Top | LayerShellAnchor::Left | LayerShellAnchor::Right,
            .height = kPanelHeight,
            .marginTop = kPanelMargin,
            .marginRight = kPanelMargin,
            .marginLeft = kPanelMargin,
            .defaultHeight = kPanelHeight,
        }
    );
    panel->surface->setRenderContext(&render);
    auto* raw = panel.get();
    panel->surface->setConfigureCallback([raw](std::uint32_t width, std::uint32_t height) {
      const auto w = static_cast<float>(width);
      const auto h = static_cast<float>(height);
      raw->root->setFrameSize(w, h);
      raw->background->setFrameSize(w, h);
    });
    panel->surface->setSceneRoot(panel->root.get());

    if (!panel->surface->initialize(output.output)) {
      kLog.warn("failed to create panel on output {}", output.connectorName);
      return nullptr;
    }
    return panel;
  }

} // namespace

int main() {
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  WaylandConnection wayland;
  if (!wayland.connect()) {
    kLog.error("failed to connect to the Wayland display");
    return 1;
  }

  GlSharedContext gl;
  gl.initialize(wayland.display());
  RenderContext render;
  render.initialize(gl);

  std::vector<std::unique_ptr<Panel>> panels;
  for (const auto& output : wayland.outputs()) {
    if (auto panel = createPanel(wayland, render, output)) {
      panels.push_back(std::move(panel));
    }
  }
  kLog.info("created {} panel(s)", panels.size());

  MainLoop loop(wayland, [] { return std::vector<PollSource*>{}; }, [&panels] { panels.clear(); });
  loop.run();
  return 0;
}
