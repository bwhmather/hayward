haywardmsg_sources = [
  'src/main.c',
]

haywardmsg_deps = [
  jsonc_dep,
  hayward_common_dep,
]

executable(
  'haywardmsg',
  haywardmsg_sources,
  include_directories: [shared_inc],
  dependencies: haywardmsg_deps,
  install: true
)
