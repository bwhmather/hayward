gnome = import('gnome')
haywardout_resources = gnome.compile_resources(
    'hwdout-resources', 'resources/hwdout.gresource.xml',
    source_dir: 'resources', c_name: 'hwdout',
)

haywardout_sources = [
	'src/main.c',
	'src/hwdout-application.c',
	'src/hwdout-configuration.c',
	'src/hwdout-configuration-head.c',
	'src/hwdout-head.c',
	'src/hwdout-head-editor.c',
	'src/hwdout-manager.c',
	'src/hwdout-mode.c',
	'src/hwdout-transform.c',
	'src/hwdout-util.c',
	'src/hwdout-window.c',
]

haywardout_deps = [
	client_protos,
	gtk,
	gtk_wayland,
	math,
	wayland_client,
]

haywardout_inc = []  # include_directories('include')


executable(
	'haywardout',
	haywardout_sources + haywardout_resources,
	include_directories: [haywardout_inc, shared_inc],
	dependencies: haywardout_deps,
	install: true
)
