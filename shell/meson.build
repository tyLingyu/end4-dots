project('ii-shell', ['c', 'cpp'],
  version: '0.0.1',
  meson_version: '>=1.1',
  default_options: [
    'cpp_std=c++23',
    'cpp_extensions=false',
    'warning_level=0',   # warnings are applied to our target only, not vendored code
    'b_ndebug=if-release',
    'b_sanitize=address,undefined',   # design decision: id references are raw pointers, ASan catches dangling ones
  ],
)

cc  = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

if get_option('buildtype') != 'release'
  add_project_arguments('-D_GLIBCXX_ASSERTIONS', language: 'cpp')
endif

# ── Dependencies ──────────────────────────────────────────────────────────────
wayland_client_dep = dependency('wayland-client')
wayland_protos_dep = dependency('wayland-protocols')
wayland_egl_dep    = dependency('wayland-egl')
egl_dep            = dependency('egl')
gles2_dep          = dependency('glesv2')
xkbcommon_dep      = dependency('xkbcommon')
freetype2_dep      = dependency('freetype2')
fontconfig_dep     = dependency('fontconfig')
cairo_dep          = dependency('cairo')
cairo_ft_dep       = dependency('cairo-ft')
pango_dep          = dependency('pango')
pangocairo_dep     = dependency('pangocairo')
pangoft2_dep       = dependency('pangoft2')
harfbuzz_dep       = dependency('harfbuzz')
librsvg_dep        = dependency('librsvg-2.0')
glib_dep           = dependency('glib-2.0')
gobject_dep        = dependency('gobject-2.0')
libwebp_dep        = dependency('libwebp')
libwebpdemux_dep   = dependency('libwebpdemux')
libjxl_dep         = dependency('libjxl')
libjxl_threads_dep = dependency('libjxl_threads')
libpipewire_dep    = dependency('libpipewire-0.3')

foreach h : ['stb/stb_image_resize2.h', 'stb/stb_image_write.h']
  if not cc.has_header(h)
    error(h + ' not found (Arch: pacman -S stb)')
  endif
endforeach

# ── Wayland protocol generation ───────────────────────────────────────────────
wayland_scanner    = find_program('wayland-scanner')
wayland_protos_dir = wayland_protos_dep.get_variable('pkgdatadir')

_protocol_sources = []
_protocol_headers = []
foreach _p : [
  ['xdg-output-unstable-v1', wayland_protos_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml'],
  ['xdg-shell', wayland_protos_dir / 'stable/xdg-shell/xdg-shell.xml'],
  ['viewporter', wayland_protos_dir / 'stable/viewporter/viewporter.xml'],
  ['idle-inhibit-unstable-v1', wayland_protos_dir / 'unstable/idle-inhibit/idle-inhibit-unstable-v1.xml'],
  # Not bound; ext-image-capture-source-v1's generated types reference its handle interface.
  ['ext-foreign-toplevel-list-v1', wayland_protos_dir / 'staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml'],
  ['xdg-activation-v1', wayland_protos_dir / 'staging/xdg-activation/xdg-activation-v1.xml'],
  ['ext-session-lock-v1', wayland_protos_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml'],
  ['ext-idle-notify-v1', wayland_protos_dir / 'staging/ext-idle-notify/ext-idle-notify-v1.xml'],
  ['ext-background-effect-v1', wayland_protos_dir / 'staging/ext-background-effect/ext-background-effect-v1.xml'],
  ['ext-image-capture-source-v1', wayland_protos_dir / 'staging/ext-image-capture-source/ext-image-capture-source-v1.xml'],
  ['ext-image-copy-capture-v1', wayland_protos_dir / 'staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml'],
  ['fractional-scale-v1', wayland_protos_dir / 'staging/fractional-scale/fractional-scale-v1.xml'],
  ['cursor-shape-v1', 'protocols/cursor-shape-v1.xml'],
  ['hyprland-focus-grab-v1', 'protocols/hyprland-focus-grab-v1.xml'],
  ['hyprland-global-shortcuts-v1', 'protocols/hyprland-global-shortcuts-v1.xml'],
  ['text-input-unstable-v3', 'protocols/text-input-unstable-v3.xml'],
  ['virtual-keyboard-unstable-v1', 'protocols/virtual-keyboard-unstable-v1.xml'],
  ['wlr-gamma-control-unstable-v1', 'protocols/wlr-gamma-control-unstable-v1.xml'],
  ['wlr-output-management-unstable-v1', 'protocols/wlr-output-management-unstable-v1.xml'],
  ['wlr-screencopy-unstable-v1', 'protocols/wlr-screencopy-unstable-v1.xml'],
]
  _protocol_headers += custom_target(_p[0] + '-client-header',
    input:   _p[1],
    output:  _p[0] + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  _protocol_sources += custom_target(_p[0] + '-client-source',
    input:   _p[1],
    output:  _p[0] + '-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach

# wlr-layer-shell: 'namespace' is a C++ keyword; rename it in the generated header only.
_wlr_ls_h = custom_target('wlr-layer-shell-unstable-v1-client-header',
  input:   'protocols/wlr-layer-shell-unstable-v1.xml',
  output:  'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [
    'sh', '-c',
    '"$1" client-header "$2" "$3" && sed -i s/namespace/name_space/g "$3"',
    '_', wayland_scanner.full_path(), '@INPUT@', '@OUTPUT@',
  ],
)
_protocol_headers += _wlr_ls_h
_protocol_sources += custom_target('wlr-layer-shell-unstable-v1-client-source',
  input:   'protocols/wlr-layer-shell-unstable-v1.xml',
  output:  'wlr-layer-shell-unstable-v1-client-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  depends: _wlr_ls_h,
)

# ── ii-shell ──────────────────────────────────────────────────────────────────
ii_sources = files(
  'src/app/main_loop.cpp',
  'src/compat/canvas_textures.cpp',
  'src/compat/color_quantizer.cpp',
  'src/compat/file_view.cpp',
  'src/compat/global_shortcuts.cpp',
  'src/compat/hyprland.cpp',
  'src/compat/ipc.cpp',
  'src/compat/json_value.cpp',
  'src/compat/notifications.cpp',
  'src/compat/panel_window.cpp',
  'src/compat/pipewire.cpp',
  'src/compat/platform.cpp',
  'src/compat/process.cpp',
  'src/compat/quickshell.cpp',
  'src/compat/system_clock.cpp',
  'src/compat/variants.cpp',
  'src/core/deferred_call.cpp',
  'src/core/files/directory_scanner.cpp',
  'src/core/files/file_watcher.cpp',
  'src/core/files/resource_paths.cpp',
  'src/core/inotify/inotify.cpp',
  'src/core/input/key_chord.cpp',
  'src/core/log.cpp',
  'src/core/lsan_suppressions.cpp',
  'src/core/process/process.cpp',
  'src/core/process/process_fds.cpp',
  'src/core/timer_manager.cpp',
  'src/core/ui_phase.cpp',
  'src/i18n/language_tag.cpp',
  'src/render/animation/animation.cpp',
  'src/render/animation/animation_manager.cpp',
  'src/render/animation/motion_service.cpp',
  'src/render/backend/gles_framebuffer.cpp',
  'src/render/backend/gles_render_backend.cpp',
  'src/render/backend/gles_texture_manager.cpp',
  'src/render/backend/render_backend.cpp',
  'src/render/core/async_texture_cache.cpp',
  'src/render/core/blur_cache.cpp',
  'src/render/core/cached_layer.cpp',
  'src/render/core/color.cpp',
  'src/render/core/image_decoder.cpp',
  'src/render/core/image_encoder.cpp',
  'src/render/core/image_file_loader.cpp',
  'src/render/core/image_orientation.cpp',
  'src/render/core/shader_program.cpp',
  'src/render/core/shared_texture_cache.cpp',
  'src/render/core/thumbnail_service.cpp',
  'src/render/gl_shared_context.cpp',
  'src/render/programs/audio_spectrum_program.cpp',
  'src/render/programs/blur_program.cpp',
  'src/render/programs/countdown_ring_program.cpp',
  'src/render/programs/effect_program.cpp',
  'src/render/programs/fancy_audio_visualizer_program.cpp',
  'src/render/programs/glyph_program.cpp',
  'src/render/programs/graph_program.cpp',
  'src/render/programs/image_program.cpp',
  'src/render/programs/linear_gradient_program.cpp',
  'src/render/programs/lockscreen_transition_program.cpp',
  'src/render/programs/rect_program.cpp',
  'src/render/programs/screen_corner_program.cpp',
  'src/render/programs/shadow_program.cpp',
  'src/render/programs/spinner_program.cpp',
  'src/render/programs/wallpaper_mask_program.cpp',
  'src/render/programs/wallpaper_program.cpp',
  'src/render/render_context.cpp',
  'src/render/render_target.cpp',
  'src/render/scene/fancy_audio_visualizer_node.cpp',
  'src/render/scene/graph_node.cpp',
  'src/render/scene/input_area.cpp',
  'src/render/scene/input_dispatcher.cpp',
  'src/render/scene/node.cpp',
  'src/render/text/cairo_glyph_renderer.cpp',
  'src/render/text/cairo_text_renderer.cpp',
  'src/render/text/font_registry.cpp',
  'src/render/text/font_weight_catalog.cpp',
  'src/render/wallpaper_renderer.cpp',
  'src/runtime/anchors.cpp',
  'src/runtime/animation.cpp',
  'src/runtime/canvas.cpp',
  'src/runtime/color.cpp',
  'src/runtime/connections.cpp',
  'src/runtime/controls.cpp',
  'src/runtime/easing.cpp',
  'src/runtime/fd_watch.cpp',
  'src/runtime/effects.cpp',
  'src/runtime/item.cpp',
  'src/runtime/js.cpp',
  'src/runtime/layout.cpp',
  'src/runtime/loader.cpp',
  'src/runtime/mouse_area.cpp',
  'src/runtime/object.cpp',
  'src/runtime/positioner.cpp',
  'src/runtime/qt.cpp',
  'src/runtime/property.cpp',
  'src/runtime/rectangle.cpp',
  'src/runtime/text.cpp',
  'src/runtime/text_layout.cpp',
  'src/runtime/timer.cpp',
  'src/system/app_identity.cpp',
  'src/system/desktop_entry.cpp',
  'src/system/internal_app_metadata.cpp',
  'src/util/checksum.cpp',
  'src/util/clamp.cpp',
  'src/wayland/hyprland/focus_grab_service.cpp',
  'src/wayland/layer_surface.cpp',
  'src/wayland/output_probe.cpp',
  'src/wayland/popup_surface.cpp',
  'src/wayland/subsurface.cpp',
  'src/wayland/surface.cpp',
  'src/wayland/text_input_service.cpp',
  'src/wayland/toplevel_surface.cpp',
  'src/wayland/virtual_keyboard_service.cpp',
  'src/wayland/wayland_connection.cpp',
  'src/wayland/wayland_seat.cpp',
)

ii_include = [
  include_directories('src'),
  include_directories('third_party/wuffs', is_system: true),
]
ii_dependencies = [
  wayland_client_dep, wayland_egl_dep, egl_dep, gles2_dep, xkbcommon_dep,
  freetype2_dep, fontconfig_dep, cairo_dep, cairo_ft_dep,
  pango_dep, pangocairo_dep, pangoft2_dep, harfbuzz_dep, librsvg_dep,
  glib_dep, gobject_dep, libwebp_dep, libwebpdemux_dep, libjxl_dep, libjxl_threads_dep,
  libpipewire_dep,
]
ii_cpp_args = [
  '-Wall', '-Wextra', '-Wno-missing-field-initializers', '-Wpedantic', '-Wconversion', '-Wshadow',
  # Vendored Noctalia code still reads these; ii-shell keeps Noctalia's defaults for now.
  '-DNOCTALIA_INSTALL_PREFIX="' + get_option('prefix') + '"',
  '-DNOCTALIA_INSTALL_DATADIR="' + get_option('datadir') + '"',
]

# stb_image_resize2 reads its coefficients through unaligned uint64_t pointers by design; UBSan's
# alignment check would abort on it, so that one translation unit is built without it.
ii_stb_resize = static_library('ii-stb-resize',
  'src/render/core/stb_image_resize_impl.cpp',
  cpp_args: ['-fno-sanitize=alignment'],
)

# Everything but main.cpp, so tests link the same code the shell runs.
ii_core = static_library('ii-core',
  ii_sources + _protocol_sources + _protocol_headers,
  include_directories: ii_include,
  dependencies: ii_dependencies,
  cpp_args: ii_cpp_args,
  link_with: ii_stb_resize,
)
ii_core_dep = declare_dependency(
  link_with: ii_core,
  sources: _protocol_headers,
  include_directories: ii_include,
  dependencies: ii_dependencies,
)

# qml2cpp's output (tools/qml2cpp): src/ii/meson.build lists it. The shell is that code, so it is
# required; generate it first (`python3 -m qml2cpp gen --deps ...`, see tools/qml2cpp/README.md).
if not import('fs').exists('src/ii/meson.build')
  error('src/ii is not generated: run tools/qml2cpp first (see shell/README.md)')
endif
subdir('src/ii')
ii_generated = static_library('ii-generated',
  ii_generated_sources,
  dependencies: ii_core_dep,
  # QML scoping: a function's parameters may share names with the component's properties.
  cpp_args: ii_cpp_args + ['-Wno-shadow'],
)
ii_generated_dep = declare_dependency(link_with: ii_generated, dependencies: ii_core_dep)

# LSan suppressions must be linked into each executable (see the file).
ii_exe_sources = get_option('b_sanitize').contains('address') ? files('src/core/lsan_suppressions.cpp') : []

executable('ii-shell',
  ['src/main.cpp'] + ii_exe_sources,
  dependencies: ii_generated_dep,
  cpp_args: ii_cpp_args,
  install: true,
)

# ── Tests ─────────────────────────────────────────────────────────────────────
nlohmann_dep = dependency('nlohmann_json')
foreach _t : [
  ['property', 'tests/runtime/property_test.cpp'],
  ['item', 'tests/runtime/item_test.cpp'],
  ['easing', 'tests/runtime/easing_test.cpp'],
  ['color', 'tests/runtime/color_test.cpp'],
  ['js', 'tests/runtime/js_test.cpp'],
  ['animation', 'tests/runtime/animation_test.cpp'],
  ['diff', 'tests/diff/diff_test.cpp'],
  ['file_view', 'tests/compat/file_view_test.cpp'],
  ['color_quantizer', 'tests/compat/color_quantizer_test.cpp'],
  ['global_shortcuts', 'tests/compat/global_shortcuts_test.cpp'],
  ['ipc', 'tests/compat/ipc_test.cpp'],
  ['hyprland', 'tests/compat/hyprland_test.cpp'],
  ['pipewire', 'tests/compat/pipewire_test.cpp'],
]
  test(_t[0], executable(_t[0] + '_test', [_t[1]] + ii_exe_sources,
    dependencies: [ii_core_dep, nlohmann_dep],
    cpp_args: ii_cpp_args + ['-DII_DIFF_EXPECTED_DIR="' + meson.current_source_dir() / 'tests/diff/expected' + '"'],
  ))
endforeach

# The generated OSD, headless.
test('osd', executable('osd_test', ['tests/generated/osd_test.cpp'] + ii_exe_sources,
  dependencies: ii_generated_dep,
  cpp_args: ii_cpp_args + ['-Wno-shadow', '-DII_DIFF_EXPECTED_DIR="' + meson.current_source_dir() / 'tests/diff/expected' + '"'],
))
