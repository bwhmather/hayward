executable(
	'wmiivnag', [
		'config.c',
		'main.c',
		'render.c',
		'wmiivnag.c',
		'types.c',
	],
	include_directories: [wmiiv_inc],
	dependencies: [
		cairo,
		client_protos,
		pango,
		pangocairo,
		wayland_client,
		wayland_cursor,
	],
	link_with: [lib_wmiiv_common, lib_wmiiv_client],
	install: true
)
