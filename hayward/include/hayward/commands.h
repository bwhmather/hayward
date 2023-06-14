#ifndef HAYWARD_COMMANDS_H
#define HAYWARD_COMMANDS_H

#include <stddef.h>
#include <stdint.h>

#include <hayward-common/list.h>

#include <hayward/config.h>
#include <hayward/input/seat.h>

struct hayward_window;

typedef struct cmd_results *
hayward_cmd(int argc, char **argv);

struct cmd_handler {
    char *command;
    hayward_cmd *handle;
};

/**
 * Indicates the result of a command's execution.
 */
enum cmd_status {
    CMD_SUCCESS, /**< The command was successful */
    CMD_FAILURE, /**< The command resulted in an error */
    CMD_INVALID, /**< Unknown command or parser error */
    CMD_DEFER,   /**< Command execution deferred */
    CMD_BLOCK,
    CMD_BLOCK_COMMANDS,
    CMD_BLOCK_END
};

/**
 * Stores the result of executing a command.
 */
struct cmd_results {
    enum cmd_status status;
    /**
     * Human friendly error message, or NULL on success
     */
    char *error;
};

enum expected_args { EXPECTED_AT_LEAST, EXPECTED_AT_MOST, EXPECTED_EQUAL_TO };

struct cmd_results *
checkarg(int argc, const char *name, enum expected_args type, int val);

const struct cmd_handler *
find_handler(
    char *line, const struct cmd_handler *cmd_handlers, size_t handlers_size
);

/**
 * Parse and executes a command.
 *
 * If the command string contains criteria then the command will be executed on
 * all matching containers. Otherwise, it'll run on the `container` container.
 * If `container` is NULL then it'll run on the currently focused container.
 */
list_t *
execute_command(
    char *command, struct hayward_seat *seat, struct hayward_window *container
);
/**
 * Parse and handles a command during config file loading.
 *
 * Do not use this under normal conditions.
 */
struct cmd_results *
config_command(char *command, char **new_block);
/**
 * Parse and handle a sub command
 */
struct cmd_results *
config_subcommand(
    char **argv, int argc, const struct cmd_handler *handlers,
    size_t handlers_size
);
/*
 * Parses a command policy rule.
 */
struct cmd_results *
config_commands_command(char *exec);
/**
 * Allocates a cmd_results object.
 */
struct cmd_results *
cmd_results_new(enum cmd_status status, const char *error, ...);
/**
 * Frees a cmd_results object.
 */
void
free_cmd_results(struct cmd_results *results);
/**
 * Serializes a list of cmd_results to a JSON string.
 *
 * Free the JSON string later on.
 */
char *
cmd_results_to_json(list_t *res_list);

void
window_resize_tiled(struct hayward_window *window, uint32_t axis, int amount);

/**
 * Handlers shared by exec and exec_always.
 */
hayward_cmd cmd_exec_validate;
hayward_cmd cmd_exec_process;

hayward_cmd cmd_bar;
hayward_cmd cmd_bindcode;
hayward_cmd cmd_bindswitch;
hayward_cmd cmd_bindsym;
hayward_cmd cmd_border;
hayward_cmd cmd_client_focused;
hayward_cmd cmd_client_focused_inactive;
hayward_cmd cmd_client_focused_tab_title;
hayward_cmd cmd_client_noop;
hayward_cmd cmd_client_unfocused;
hayward_cmd cmd_client_urgent;
hayward_cmd cmd_create_output;
hayward_cmd cmd_default_border;
hayward_cmd cmd_default_floating_border;
hayward_cmd cmd_exec;
hayward_cmd cmd_exec_always;
hayward_cmd cmd_exit;
hayward_cmd cmd_floating;
hayward_cmd cmd_floating_maximum_size;
hayward_cmd cmd_floating_minimum_size;
hayward_cmd cmd_floating_modifier;
hayward_cmd cmd_focus;
hayward_cmd cmd_focus_follows_mouse;
hayward_cmd cmd_focus_on_window_activation;
hayward_cmd cmd_focus_wrapping;
hayward_cmd cmd_font;
hayward_cmd cmd_force_display_urgency_hint;
hayward_cmd cmd_fullscreen;
hayward_cmd cmd_haywardbg_command;
hayward_cmd cmd_haywardnag_command;
hayward_cmd cmd_hide_edge_borders;
hayward_cmd cmd_include;
hayward_cmd cmd_inhibit_idle;
hayward_cmd cmd_input;
hayward_cmd cmd_kill;
hayward_cmd cmd_layout;
hayward_cmd cmd_max_render_time;
hayward_cmd cmd_mode;
hayward_cmd cmd_move;
hayward_cmd cmd_nop;
hayward_cmd cmd_opacity;
hayward_cmd cmd_output;
hayward_cmd cmd_popup_during_fullscreen;
hayward_cmd cmd_reload;
hayward_cmd cmd_resize;
hayward_cmd cmd_seat;
hayward_cmd cmd_set;
hayward_cmd cmd_shortcuts_inhibitor;
hayward_cmd cmd_smart_borders;
hayward_cmd cmd_tiling_drag;
hayward_cmd cmd_tiling_drag_threshold;
hayward_cmd cmd_title_align;
hayward_cmd cmd_titlebar_border_thickness;
hayward_cmd cmd_titlebar_padding;
hayward_cmd cmd_title_format;
hayward_cmd cmd_unbindcode;
hayward_cmd cmd_unbindswitch;
hayward_cmd cmd_unbindsym;
hayward_cmd cmd_urgent;
hayward_cmd cmd_workspace;
hayward_cmd cmd_xwayland;

hayward_cmd bar_cmd_bindcode;
hayward_cmd bar_cmd_binding_mode_indicator;
hayward_cmd bar_cmd_bindsym;
hayward_cmd bar_cmd_colors;
hayward_cmd bar_cmd_haywardbar_command;
hayward_cmd bar_cmd_height;
hayward_cmd bar_cmd_hidden_state;
hayward_cmd bar_cmd_icon_theme;
hayward_cmd bar_cmd_id;
hayward_cmd bar_cmd_mode;
hayward_cmd bar_cmd_modifier;
hayward_cmd bar_cmd_output;
hayward_cmd bar_cmd_pango_markup;
hayward_cmd bar_cmd_position;
hayward_cmd bar_cmd_separator_symbol;
hayward_cmd bar_cmd_status_command;
hayward_cmd bar_cmd_status_edge_padding;
hayward_cmd bar_cmd_status_padding;
hayward_cmd bar_cmd_strip_workspace_name;
hayward_cmd bar_cmd_strip_workspace_numbers;
hayward_cmd bar_cmd_tray_bindcode;
hayward_cmd bar_cmd_tray_bindsym;
hayward_cmd bar_cmd_tray_output;
hayward_cmd bar_cmd_tray_padding;
hayward_cmd bar_cmd_unbindcode;
hayward_cmd bar_cmd_unbindsym;
hayward_cmd bar_cmd_workspace_buttons;
hayward_cmd bar_cmd_workspace_min_width;
hayward_cmd bar_cmd_wrap_scroll;

hayward_cmd bar_colors_cmd_active_workspace;
hayward_cmd bar_colors_cmd_background;
hayward_cmd bar_colors_cmd_binding_mode;
hayward_cmd bar_colors_cmd_focused_background;
hayward_cmd bar_colors_cmd_focused_separator;
hayward_cmd bar_colors_cmd_focused_statusline;
hayward_cmd bar_colors_cmd_focused_workspace;
hayward_cmd bar_colors_cmd_inactive_workspace;
hayward_cmd bar_colors_cmd_separator;
hayward_cmd bar_colors_cmd_statusline;
hayward_cmd bar_colors_cmd_urgent_workspace;

hayward_cmd input_cmd_accel_profile;
hayward_cmd input_cmd_calibration_matrix;
hayward_cmd input_cmd_click_method;
hayward_cmd input_cmd_drag;
hayward_cmd input_cmd_drag_lock;
hayward_cmd input_cmd_dwt;
hayward_cmd input_cmd_events;
hayward_cmd input_cmd_left_handed;
hayward_cmd input_cmd_map_from_region;
hayward_cmd input_cmd_map_to_output;
hayward_cmd input_cmd_map_to_region;
hayward_cmd input_cmd_middle_emulation;
hayward_cmd input_cmd_natural_scroll;
hayward_cmd input_cmd_pointer_accel;
hayward_cmd input_cmd_repeat_delay;
hayward_cmd input_cmd_repeat_rate;
hayward_cmd input_cmd_scroll_button;
hayward_cmd input_cmd_scroll_factor;
hayward_cmd input_cmd_scroll_method;
hayward_cmd input_cmd_tap;
hayward_cmd input_cmd_tap_button_map;
hayward_cmd input_cmd_tool_mode;
hayward_cmd input_cmd_xkb_capslock;
hayward_cmd input_cmd_xkb_file;
hayward_cmd input_cmd_xkb_layout;
hayward_cmd input_cmd_xkb_model;
hayward_cmd input_cmd_xkb_numlock;
hayward_cmd input_cmd_xkb_options;
hayward_cmd input_cmd_xkb_rules;
hayward_cmd input_cmd_xkb_switch_layout;
hayward_cmd input_cmd_xkb_variant;

hayward_cmd output_cmd_adaptive_sync;
hayward_cmd output_cmd_background;
hayward_cmd output_cmd_disable;
hayward_cmd output_cmd_dpms;
hayward_cmd output_cmd_enable;
hayward_cmd output_cmd_max_render_time;
hayward_cmd output_cmd_mode;
hayward_cmd output_cmd_modeline;
hayward_cmd output_cmd_position;
hayward_cmd output_cmd_render_bit_depth;
hayward_cmd output_cmd_scale;
hayward_cmd output_cmd_scale_filter;
hayward_cmd output_cmd_subpixel;
hayward_cmd output_cmd_toggle;
hayward_cmd output_cmd_transform;

hayward_cmd seat_cmd_attach;
hayward_cmd seat_cmd_fallback;
hayward_cmd seat_cmd_hide_cursor;
hayward_cmd seat_cmd_idle_inhibit;
hayward_cmd seat_cmd_idle_wake;
hayward_cmd seat_cmd_keyboard_grouping;
hayward_cmd seat_cmd_pointer_constraint;
hayward_cmd seat_cmd_shortcuts_inhibitor;
hayward_cmd seat_cmd_xcursor_theme;

#endif
