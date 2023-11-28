hayward_common_sources = [
  'src/background-image.c',
  'src/cairo.c',
  'src/ipc-client.c',
  'src/log.c',
  'src/loop.c',
  'src/list.c',
  'src/pango.c',
  'src/stringop.c',
  'src/util.c'
]

hayward_common_inc = include_directories('include')

hayward_common_lib = static_library(
  'hayward-common',
  hayward_common_sources,
  dependencies: [
    cairo_dep,
    gdk_pixbuf_dep,
    pango_dep,
    pango_cairo_dep,
    wayland_client_dep.partial_dependency(compile_args: true)
  ],
  include_directories: [hayward_common_inc, shared_inc]
)

hayward_common_dep = declare_dependency(
  include_directories : hayward_common_inc,
  link_with: hayward_common_lib,
)
