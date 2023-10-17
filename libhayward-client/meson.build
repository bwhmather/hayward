hayward_client_sources = [
  'src/pool-buffer.c',
]

hayward_client_inc = include_directories('include')

hayward_client_lib = static_library(
  'hayward-client',
  hayward_client_sources,
  dependencies: [
    cairo,
    pango,
    pangocairo,
    wayland_client,
    hayward_common_dep,
  ],
  include_directories: [hayward_client_inc, shared_inc]
)

hayward_client_dep = declare_dependency(
  include_directories : hayward_client_inc,
  link_with: hayward_client_lib,
)
