lib_hayward_client = static_library(
	'hayward-client',
	files(
		'pool-buffer.c',
	),
	dependencies: [
		cairo,
		pango,
		pangocairo,
		wayland_client
	],
	link_with: [lib_hayward_common],
	include_directories: hayward_inc
)
