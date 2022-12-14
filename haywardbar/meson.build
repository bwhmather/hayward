tray_files = have_tray ? [
	'tray/host.c',
	'tray/icon.c',
	'tray/item.c',
	'tray/tray.c',
	'tray/watcher.c'
] : []

haywardbar_deps = [
	cairo,
	client_protos,
	gdk_pixbuf,
	jsonc,
	math,
	pango,
	pangocairo,
	rt,
	wayland_client,
	wayland_cursor
]
if have_tray
	haywardbar_deps += sdbus
endif

haywardbar_inc = include_directories('include')

executable(
	'haywardbar', [
		'bar.c',
		'config.c',
		'i3bar.c',
		'input.c',
		'ipc.c',
		'main.c',
		'render.c',
		'status_line.c',
		tray_files
	],
	include_directories: [haywardbar_inc, shared_inc],
	dependencies: haywardbar_deps,
	link_with: [lib_hayward_common, lib_hayward_client],
	install: true
)
