lib_hayward_common_sources = [
	'src/background-image.c',
	'src/cairo.c',
	'src/ipc-client.c',
	'src/log.c',
	'src/loop.c',
	'src/list.c',
	'src/pango.c',
	'src/stringop.c',
	'src/util.c'
]

lib_hayward_common = static_library(
	'hayward-common',
	lib_hayward_common_sources,
	dependencies: [
		cairo,
		gdk_pixbuf,
		pango,
		pangocairo,
		wayland_client.partial_dependency(compile_args: true)
	],
	include_directories: [shared_inc]
)
