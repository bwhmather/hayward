gnome = import('gnome')
haywardout_resources = gnome.compile_resources(
  'hwdout-resources', 'haywardout/resources/hwdout.gresource.xml',
  source_dir: 'haywardout/resources', c_name: 'hwdout',
)

haywardout_sources = [
  'haywardout/main.c',
  'haywardout/hwdout-application.c',
  'haywardout/hwdout-configuration.c',
  'haywardout/hwdout-configuration-head.c',
  'haywardout/hwdout-head.c',
  'haywardout/hwdout-head-editor.c',
  'haywardout/hwdout-manager.c',
  'haywardout/hwdout-mode.c',
  'haywardout/hwdout-transform.c',
  'haywardout/hwdout-util.c',
  'haywardout/hwdout-window.c',
]

haywardout_deps = [
  client_protos_dep,
  gtk_dep,
  gtk_wayland_dep,
  math_dep,
  wayland_client_dep,
]

executable(
  'haywardout',
  haywardout_sources + haywardout_resources,
  dependencies: haywardout_deps,
  install: true
)
