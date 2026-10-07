// ii-shell: the panels ported so far (qml2cpp's output in src/ii), on Hyprland.
//
//   ii-shell                     run the shell
//   ii-shell ipc call|show ...   talk to a running one, as `qs ipc`

#include "app/deferred_call_poll_source.h"
#include "app/main_loop.h"
#include "app/timer_poll_source.h"
#include "compat/ipc.h"
#include "compat/platform.h"
#include "core/log.h"
#include "ii/components.h"
#include "ii/modules/ii/onScreenDisplay/OnScreenDisplay.h"
#include "ii/services/Hyprsunset.h"
#include "ii/services/MaterialThemeLoader.h"
#include "render/gl_shared_context.h"
#include "render/render_context.h"
#include "runtime/fd_watch.h"
#include "runtime/loader.h"
#include "runtime/text.h"
#include "wayland/wayland_connection.h"

#include <csignal>
#include <cstring>
#include <memory>
#include <vector>

namespace {

  constexpr Logger kLog("ii-shell");

  void onSignal(int /*signal*/) { MainLoop::requestShutdown(); }

} // namespace

int main(int argc, char** argv) {
  if (argc > 1 && (std::strcmp(argv[1], "ipc") == 0 || std::strcmp(argv[1], "msg") == 0)) {
    return ii::qs::handleIpcCli(argc, argv);
  }

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
  ii::qs::setPlatform(&wayland, &render);

  ii::Loader::setResolver(ii::generatedComponent);
  // shell.qml's Component.onCompleted, for the services ported so far (singletons load lazily).
  ii::services::MaterialThemeLoader::instance().reapplyTheme();
  ii::services::Hyprsunset::instance().load();
  auto osd = ii::createRoot<ii::onScreenDisplay::OnScreenDisplay>();
  ii::qs::IpcServer::start();

  TimerPollSource timers;
  DeferredCallPollSource deferred;
  MainLoop loop(
      wayland,
      [&timers, &deferred] { return std::vector<PollSource*>{&ii::FdWatch::pollSource(), &timers, &deferred}; },
      [&osd] {
        ii::qs::IpcServer::stop();
        osd.reset();
        ii::qs::setPlatform(nullptr, nullptr);
      }
  );
  loop.run();
  return 0;
}
