tray_files = have_tray ? [
	'tray/host.c',
	'tray/icon.c',
	'tray/item.c',
	'tray/tray.c',
	'tray/watcher.c'
] : []

wmiivbar_deps = [
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
	wmiivbar_deps += sdbus
endif

executable(
	'wmiivbar', [
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
	include_directories: [wmiiv_inc],
	dependencies: wmiivbar_deps,
	link_with: [lib_wmiiv_common, lib_wmiiv_client],
	install: true
)
