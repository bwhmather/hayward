haywardout_sources = [
	'src/main.c',
]

haywardout_deps = [
	client_protos,
	gtk,
	gtk_wayland,
	math,
	wayland_client,
]

haywardout_inc = include_directories('include')

executable(
	'haywardout',
	haywardout_sources,
	include_directories: [haywardout_inc, shared_inc],
	dependencies: haywardout_deps,
	install: true
)
