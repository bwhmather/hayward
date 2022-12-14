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
	cairo,
	client_protos,
	gdk_pixbuf,
	jsonc,
	math,
	pango,
	pangocairo,
	rt,
	wayland_client,
	wayland_cursor,
	hayward_client_dep,
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
	link_with: [lib_hayward_common],
	install: true
)
