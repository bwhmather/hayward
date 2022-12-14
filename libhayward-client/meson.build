lib_hayward_client_sources = [
	'src/pool-buffer.c',
]

lib_hayward_client = static_library(
	'hayward-client',
	lib_hayward_client_sources,
	dependencies: [
		cairo,
		pango,
		pangocairo,
		wayland_client
	],
	link_with: [lib_hayward_common],
	include_directories: [shared_inc]
)
