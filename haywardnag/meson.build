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
		cairo,
		client_protos,
		pango,
		pangocairo,
		wayland_client,
		wayland_cursor,
		hayward_client_dep,
	],
	link_with: [lib_hayward_common],
	install: true
)
