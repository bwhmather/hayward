project(
  'hayward',
  'c',
  version: '0.1-dev',
  license: 'MIT',
  meson_version: '>=0.60.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=true',
  ],
)

add_project_arguments([
    '-DWLR_USE_UNSTABLE',

    '-Wno-unused-parameter',
    '-Wundef',
    '-Wvla',
    '-Wno-overlength-strings',
  ],
  language: 'c',
)

cc = meson.get_compiler('c')

is_freebsd = host_machine.system().startswith('freebsd')
datadir = get_option('datadir')
sysconfdir = get_option('sysconfdir')
prefix = get_option('prefix')

if is_freebsd
  add_project_arguments('-D_C11_SOURCE', language: 'c')
endif

# Execute the wlroots subproject, if any
wlroots_version = ['>=0.18.0', '<0.19.0']
subproject(
  'wlroots',
  default_options: ['examples=false'],
  required: false,
  version: wlroots_version,
)

sysprof_dep = dependency('sysprof-capture-4', required: false, include_type: 'system')
wayland_server_dep = dependency('wayland-server', version: '>=1.21.0')
wayland_cursor_dep = dependency('wayland-cursor')
wayland_protos_dep = dependency('wayland-protocols', version: '>=1.24')
wlroots_dep = dependency('wlroots-0.18', version: wlroots_version, include_type: 'system')
xkbcommon_dep = dependency('xkbcommon')
cairo_dep = dependency('cairo')
pango_dep = dependency('pango')
pango_cairo_dep = dependency('pangocairo')
pixman_dep = dependency('pixman-1')
libevdev_dep = dependency('libevdev')
libinput_dep = dependency('libinput', version: '>=1.6.0')
xcb_dep = dependency('xcb', required: get_option('xwayland'))
drm_full_dep = dependency('libdrm') # only needed for drm_fourcc.h
drm_dep = drm_full_dep.partial_dependency(compile_args: true, includes: true)
libudev_dep = dependency('libudev')
math_dep = cc.find_library('m')
rt_dep = cc.find_library('rt')
xcb_icccm_dep = dependency('xcb-icccm', required: get_option('xwayland'))

wlroots_features = {
  'xwayland': false,
}
foreach name, _ : wlroots_features
  var_name = 'have_' + name.underscorify()
  have = wlroots_dep.get_variable(pkgconfig: var_name, internal: var_name) == 'true'
  wlroots_features += { name: have }
endforeach

if get_option('xwayland').enabled() and not wlroots_features['xwayland']
  error('Cannot enable Xwayland in hayward: wlroots has been built without Xwayland support')
endif
have_xwayland = xcb_dep.found() and wlroots_features['xwayland']

if get_option('sd-bus-provider') == 'auto'
  assert(get_option('auto_features').auto(), 'sd-bus-provider must not be set to auto since auto_features != auto')
  sdbus = dependency(['libsystemd', 'libelogind'],
    required: false,
    version: '>=239',
  )
  if not sdbus.found()
    sdbus = dependency('basu', required: false)
  endif
else
  sdbus = dependency(get_option('sd-bus-provider'))
endif

conf_data = configuration_data()

conf_data.set10('HAVE_XWAYLAND', have_xwayland)
conf_data.set10('HAVE_LIBSYSTEMD', sdbus.found() and sdbus.name() == 'libsystemd')
conf_data.set10('HAVE_LIBELOGIND', sdbus.found() and sdbus.name() == 'libelogind')
conf_data.set10('HAVE_BASU', sdbus.found() and sdbus.name() == 'basu')
conf_data.set10('HAVE_SYSPROF', sysprof_dep.found())

add_project_arguments('-DSYSCONFDIR="/@0@"'.format(join_paths(prefix, sysconfdir)), language : 'c')

version = '"@0@"'.format(meson.project_version())
git = find_program('git', native: true, required: false)
if git.found()
  git_commit = run_command([git, 'rev-parse', '--short', 'HEAD'], check: false)
  git_branch = run_command([git, 'rev-parse', '--abbrev-ref', 'HEAD'], check: false)
  if git_commit.returncode() == 0 and git_branch.returncode() == 0
    version = '"@0@-@1@ (" __DATE__ ", branch \'@2@\')"'.format(
      meson.project_version(),
      git_commit.stdout().strip(),
      git_branch.stdout().strip(),
    )
  endif
endif
add_project_arguments('-DHWD_VERSION=@0@'.format(version), language: 'c')

# Compute the relative path used by compiler invocations.
source_root = meson.current_source_dir().split('/')
build_root = meson.global_build_root().split('/')
relative_dir_parts = []
i = 0
in_prefix = true
foreach p : build_root
  if i >= source_root.length() or not in_prefix or p != source_root[i]
    in_prefix = false
    relative_dir_parts += '..'
  endif
  i += 1
endforeach
i = 0
in_prefix = true
foreach p : source_root
  if i >= build_root.length() or not in_prefix or build_root[i] != p
    in_prefix = false
    relative_dir_parts += p
  endif
  i += 1
endforeach
relative_dir = join_paths(relative_dir_parts) + '/'

# Strip relative path prefixes from the code if possible, otherwise hide them.
if cc.has_argument('-fmacro-prefix-map=/prefix/to/hide=')
  add_project_arguments(
    '-fmacro-prefix-map=@0@='.format(relative_dir),
    language: 'c',
  )
else
  add_project_arguments(
    '-DHWD_REL_SRC_DIR="@0@"'.format(relative_dir),
    language: 'c',
  )
endif

shared_inc = include_directories('include')

subdir('include')
subdir('protocols')

hayward_sources = files(
  'src/commands.c',
  'src/config.c',
  'src/haywardnag.c',
  'src/lock.c',
  'src/main.c',
  'src/scheduler.c',
  'src/server.c',
  'src/theme.c',

  'src/desktop/hwd_workspace_management_v1.c',
  'src/desktop/idle_inhibit_v1.c',
  'src/desktop/layer_shell.c',
  'src/desktop/server_decoration.c',
  'src/desktop/wlr_output_management_v1.c',
  'src/desktop/xdg_activation_v1.c',
  'src/desktop/xdg_decoration.c',
  'src/desktop/xdg_shell.c',

  'src/input/input_manager.c',
  'src/input/cursor.c',
  'src/input/keyboard.c',
  'src/input/libinput.c',
  'src/input/seat.c',
  'src/input/seatop_default.c',
  'src/input/seatop_down.c',
  'src/input/seatop_move.c',
  'src/input/seatop_resize_floating.c',
  'src/input/seatop_resize_tiling.c',
  'src/input/switch.c',
  'src/input/tablet.c',
  'src/input/text_input.c',

  'src/config/seat.c',
  'src/config/input.c',

  'src/commands/bind.c',
  'src/commands/exit.c',
  'src/commands/exec.c',
  'src/commands/exec_always.c',
  'src/commands/floating.c',
  'src/commands/floating_minmax_size.c',
  'src/commands/floating_modifier.c',
  'src/commands/focus.c',
  'src/commands/focus_follows_mouse.c',
  'src/commands/focus_on_window_activation.c',
  'src/commands/focus_wrapping.c',
  'src/commands/font.c',
  'src/commands/force_display_urgency_hint.c',
  'src/commands/fullscreen.c',
  'src/commands/kill.c',
  'src/commands/include.c',
  'src/commands/input.c',
  'src/commands/layout.c',
  'src/commands/mode.c',
  'src/commands/move.c',
  'src/commands/nop.c',
  'src/commands/reload.c',
  'src/commands/resize.c',
  'src/commands/seat.c',
  'src/commands/seat/attach.c',
  'src/commands/seat/fallback.c',
  'src/commands/seat/hide_cursor.c',
  'src/commands/seat/idle.c',
  'src/commands/seat/keyboard_grouping.c',
  'src/commands/seat/pointer_constraint.c',
  'src/commands/seat/xcursor_theme.c',
  'src/commands/set.c',
  'src/commands/haywardnag_command.c',
  'src/commands/tiling_drag_threshold.c',
  'src/commands/workspace.c',
  'src/commands/xwayland.c',

  'src/commands/input/accel_profile.c',
  'src/commands/input/calibration_matrix.c',
  'src/commands/input/click_method.c',
  'src/commands/input/drag.c',
  'src/commands/input/drag_lock.c',
  'src/commands/input/dwt.c',
  'src/commands/input/events.c',
  'src/commands/input/left_handed.c',
  'src/commands/input/map_from_region.c',
  'src/commands/input/map_to_output.c',
  'src/commands/input/map_to_region.c',
  'src/commands/input/middle_emulation.c',
  'src/commands/input/natural_scroll.c',
  'src/commands/input/pointer_accel.c',
  'src/commands/input/repeat_delay.c',
  'src/commands/input/repeat_rate.c',
  'src/commands/input/scroll_button.c',
  'src/commands/input/scroll_factor.c',
  'src/commands/input/scroll_method.c',
  'src/commands/input/tap.c',
  'src/commands/input/tap_button_map.c',
  'src/commands/input/tool_mode.c',
  'src/commands/input/xkb_capslock.c',
  'src/commands/input/xkb_file.c',
  'src/commands/input/xkb_layout.c',
  'src/commands/input/xkb_model.c',
  'src/commands/input/xkb_numlock.c',
  'src/commands/input/xkb_options.c',
  'src/commands/input/xkb_rules.c',
  'src/commands/input/xkb_switch_layout.c',
  'src/commands/input/xkb_variant.c',

  'src/scene/cairo.c',
  'src/scene/colours.c',
  'src/scene/nineslice.c',
  'src/scene/text.c',

  'src/tree/column.c',
  'src/tree/output.c',
  'src/tree/root.c',
  'src/tree/transaction.c',
  'src/tree/view.c',
  'src/tree/window.c',
  'src/tree/workspace.c',

  'src/list.c',
  'src/pango.c',
  'src/stringop.c',
  'src/util.c'
)

hayward_deps = [
  cairo_dep,
  drm_dep,
  libevdev_dep,
  libinput_dep,
  libudev_dep,
  math_dep,
  pango_dep,
  pango_cairo_dep,
  pixman_dep,
  server_protos_dep,
  sysprof_dep,
  wayland_server_dep,
  wlroots_dep,
  xkbcommon_dep,
  xcb_icccm_dep,
]

if have_xwayland
  hayward_sources += 'src/desktop/xwayland.c'
  hayward_deps += xcb_dep
endif

hayward_inc = include_directories('include')

executable(
  'hayward',
  hayward_sources,
  include_directories: [hayward_inc, shared_inc],
  dependencies: hayward_deps,
  install: true
)

config = configuration_data()
config.set('datadir', join_paths(prefix, datadir))
config.set('prefix', prefix)
config.set('sysconfdir', join_paths(prefix, sysconfdir))

configure_file(
  configuration: config,
  input: 'config.in',
  output: '@BASENAME@',
  install_dir: join_paths(sysconfdir, 'hayward')
)

install_data(
  'hayward.desktop',
  install_dir: join_paths(datadir, 'wayland-sessions')
)

test_suites = {
  'lint': [
    'commands-and-headers-match',
    'exact-includes',
    'include-guards',
    'no-circular-includes',
    'source-and-header-contents-match',
    'source-and-header-orders-match',
    'source-heading',
    'tree-header-orders-match',
    'unused-symbols',
  ],
}

pymod = import('python')
python = pymod.find_installation('python3')

foreach suite, tests : test_suites
  foreach test_name : tests
    test(
      suite + '-' + test_name,
      python,
      args: [
        meson.current_source_dir() +
        '/tests/' + suite +
        '/test_' + test_name.replace('-', '_') + '.py',
      ],
      env: {
        'MESON_BUILD_ROOT': meson.project_build_root(),
        'MESON_SOURCE_ROOT': meson.project_source_root(),
      },
      timeout: 1000,
    )
  endforeach
endforeach

summary({
  'xwayland': have_xwayland,
}, bool_yn: true)

