haywardnag_sources = [
  'src/config.c',
  'src/main.c',
  'src/render.c',
  'src/haywardnag.c',
  'src/types.c',
]

haywardnag_inc = include_directories('include')

executable(
  'haywardnag',
  haywardnag_sources,
  include_directories: [haywardnag_inc, shared_inc],
  dependencies: [
    cairo_dep,
    client_protos_dep,
    pango_dep,
    pango_cairo_dep,
    wayland_client_dep,
    wayland_cursor_dep,
    hayward_client_dep,
    hayward_common_dep,
  ],
  install: true
)
