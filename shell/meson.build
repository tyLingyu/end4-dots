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
  ['ext-workspace-v1', wayland_protos_dir / 'staging/ext-workspace/ext-workspace-v1.xml'],
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
  ['hyprland-toplevel-mapping-v1', 'protocols/hyprland-toplevel-mapping-v1.xml'],
  ['text-input-unstable-v3', 'protocols/text-input-unstable-v3.xml'],
  ['virtual-keyboard-unstable-v1', 'protocols/virtual-keyboard-unstable-v1.xml'],
  ['wlr-foreign-toplevel-management-unstable-v1', 'protocols/wlr-foreign-toplevel-management-unstable-v1.xml'],
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
  'src/render/core/stb_image_resize_impl.cpp',
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
  'src/runtime/color.cpp',
  'src/runtime/easing.cpp',
  'src/runtime/item.cpp',
  'src/runtime/js.cpp',
  'src/runtime/layout.cpp',
  'src/runtime/object.cpp',
  'src/runtime/positioner.cpp',
  'src/runtime/property.cpp',
  'src/runtime/rectangle.cpp',
  'src/runtime/text.cpp',
  'src/runtime/text_layout.cpp',
  'src/system/app_identity.cpp',
  'src/system/desktop_entry.cpp',
  'src/system/internal_app_metadata.cpp',
  'src/util/checksum.cpp',
  'src/util/clamp.cpp',
  'src/wayland/ext_foreign_toplevels.cpp',
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
  'src/wayland/wayland_toplevels.cpp',
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
]
ii_cpp_args = [
  '-Wall', '-Wextra', '-Wno-missing-field-initializers', '-Wpedantic', '-Wconversion', '-Wshadow',
  # Vendored Noctalia code still reads these; ii-shell keeps Noctalia's defaults for now.
  '-DNOCTALIA_INSTALL_PREFIX="' + get_option('prefix') + '"',
  '-DNOCTALIA_INSTALL_DATADIR="' + get_option('datadir') + '"',
]

# Everything but main.cpp, so tests link the same code the shell runs.
ii_core = static_library('ii-core',
  ii_sources + _protocol_sources + _protocol_headers,
  include_directories: ii_include,
  dependencies: ii_dependencies,
  cpp_args: ii_cpp_args,
)
ii_core_dep = declare_dependency(
  link_with: ii_core,
  sources: _protocol_headers,
  include_directories: ii_include,
  dependencies: ii_dependencies,
)

# LSan suppressions must be linked into each executable (see the file).
ii_exe_sources = get_option('b_sanitize').contains('address') ? files('src/core/lsan_suppressions.cpp') : []

executable('ii-shell',
  ['src/main.cpp'] + ii_exe_sources,
  dependencies: ii_core_dep,
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
]
  test(_t[0], executable(_t[0] + '_test', [_t[1]] + ii_exe_sources,
    dependencies: [ii_core_dep, nlohmann_dep],
    cpp_args: ii_cpp_args + ['-DII_DIFF_EXPECTED_DIR="' + meson.current_source_dir() / 'tests/diff/expected' + '"'],
  ))
endforeach
