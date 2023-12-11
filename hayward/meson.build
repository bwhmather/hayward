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
  'src/commands/max_render_time.c',
  'src/commands/opacity.c',
  'src/commands/include.c',
  'src/commands/input.c',
  'src/commands/layout.c',
  'src/commands/mode.c',
  'src/commands/move.c',
  'src/commands/nop.c',
  'src/commands/popup_during_fullscreen.c',
  'src/commands/reload.c',
  'src/commands/resize.c',
  'src/commands/seat.c',
  'src/commands/seat/attach.c',
  'src/commands/seat/fallback.c',
  'src/commands/seat/hide_cursor.c',
  'src/commands/seat/idle.c',
  'src/commands/seat/keyboard_grouping.c',
  'src/commands/seat/pointer_constraint.c',
  'src/commands/seat/shortcuts_inhibitor.c',
  'src/commands/seat/xcursor_theme.c',
  'src/commands/set.c',
  'src/commands/shortcuts_inhibitor.c',
  'src/commands/haywardnag_command.c',
  'src/commands/tiling_drag_threshold.c',
  'src/commands/urgent.c',
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
  'src/tree.c',

  'src/log.c',
  'src/list.c',
  'src/pango.c',
  'src/stringop.c',
  'src/util.c'
)

hayward_deps = [
  cairo_dep,
  drm_dep,
  gdk_pixbuf_dep,
  jsonc_dep,
  libevdev_dep,
  libinput_dep,
  libudev_dep,
  math_dep,
  pango_dep,
  pango_cairo_dep,
  pixman_dep,
  server_protos_dep,
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

test_suites = {
  'lint': [
    'exact-includes',
    'unused-symbols',
    'include-guards',
    'no-circular-includes',
    'commands-and-headers-match',
    'tree-header-orders-match',
    'source-and-header-orders-match',
    'source-and-header-contents-match',
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
