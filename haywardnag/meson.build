executable(
	'haywardnag', [
		'config.c',
		'main.c',
		'render.c',
		'haywardnag.c',
		'types.c',
	],
	include_directories: [hayward_inc],
	dependencies: [
		cairo,
		client_protos,
		pango,
		pangocairo,
		wayland_client,
		wayland_cursor,
	],
	link_with: [lib_hayward_common, lib_hayward_client],
	install: true
)
