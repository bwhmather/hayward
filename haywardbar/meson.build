haywardbar_sources = [
  'src/bar.c',
  'src/config.c',
  'src/i3bar.c',
  'src/input.c',
  'src/ipc.c',
  'src/main.c',
  'src/render.c',
  'src/status_line.c',
]

if have_tray
  haywardbar_sources += [
    'src/tray/host.c',
    'src/tray/icon.c',
    'src/tray/item.c',
    'src/tray/tray.c',
    'src/tray/watcher.c'
  ]
endif

haywardbar_deps = [
  cairo_dep,
  client_protos_dep,
  gdk_pixbuf_dep,
  jsonc_dep,
  math_dep,
  pango_dep,
  pango_cairo_dep,
  rt_dep,
  wayland_client_dep,
  wayland_cursor_dep,
  hayward_client_dep,
  hayward_common_dep,
]
if have_tray
  haywardbar_deps += sdbus
endif

haywardbar_inc = include_directories('include')

executable(
  'haywardbar',
  haywardbar_sources,
  include_directories: [haywardbar_inc, shared_inc],
  dependencies: haywardbar_deps,
  install: true
)
