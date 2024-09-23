wl_protocol_dir = wayland_protos_dep.get_variable('pkgdatadir')

wayland_scanner_dep = dependency('wayland-scanner', required: false, native: true)
if wayland_scanner_dep.found()
  wayland_scanner = find_program(
    wayland_scanner_dep.get_variable(pkgconfig: 'wayland_scanner'),
    native: true,
  )
else
  wayland_scanner = find_program('wayland-scanner', native: true)
endif

protocols = [
  [wl_protocol_dir, 'stable/tablet/tablet-v2.xml'],
  [wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
  [wl_protocol_dir, 'unstable/xdg-output/xdg-output-unstable-v1.xml'],
  [wl_protocol_dir, 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'],
  [wl_protocol_dir, 'unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml'],
  ['hwd-workspace-management-unstable-v1.xml'],
  ['wlr-layer-shell-unstable-v1.xml'],
  ['idle.xml'],
  ['wlr-input-inhibitor-unstable-v1.xml'],
  ['wlr-output-management-unstable-v1.xml'],
]

wl_protos_src = []
wl_protos_headers = []

foreach p : protocols
  xml = join_paths(p)
  wl_protos_src += custom_target(
    xml.underscorify() + '_server_c',
    input: xml,
    output: '@BASENAME@-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
  wl_protos_headers += custom_target(
    xml.underscorify() + '_server_h',
    input: xml,
    output: '@BASENAME@-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach

lib_server_protos = static_library(
  'server_protos',
  wl_protos_src + wl_protos_headers,
  dependencies: wayland_server_dep.partial_dependency(compile_args: true),
)

server_protos_dep = declare_dependency(
  link_with: lib_server_protos,
  sources: wl_protos_headers,
)
