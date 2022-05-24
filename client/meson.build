lib_wmiiv_client = static_library(
	'wmiiv-client',
	files(
		'pool-buffer.c',
	),
	dependencies: [
		cairo,
		pango,
		pangocairo,
		wayland_client
	],
	link_with: [lib_wmiiv_common],
	include_directories: wmiiv_inc
)
