haywardmsg_sources = [
	'src/main.c',
]

executable(
	'haywardmsg',
	haywardmsg_sources,
	include_directories: [shared_inc],
	dependencies: [jsonc],
	link_with: [lib_hayward_common],
	install: true
)
