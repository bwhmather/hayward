#ifndef _WMIIV_COMMANDS_H
#define _WMIIV_COMMANDS_H

#include <wlr/util/edges.h>
#include "config.h"

struct wmiiv_container;

typedef struct cmd_results *wmiiv_cmd(int argc, char **argv);

struct cmd_handler {
	char *command;
	wmiiv_cmd *handle;
};

/**
 * Indicates the result of a command's execution.
 */
enum cmd_status {
	CMD_SUCCESS, 		/**< The command was successful */
	CMD_FAILURE,		/**< The command resulted in an error */
	CMD_INVALID, 		/**< Unknown command or parser error */
	CMD_DEFER,		/**< Command execution deferred */
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

enum expected_args {
	EXPECTED_AT_LEAST,
	EXPECTED_AT_MOST,
	EXPECTED_EQUAL_TO
};

struct cmd_results *checkarg(int argc, const char *name,
		enum expected_args type, int val);

const struct cmd_handler *find_handler(char *line,
		const struct cmd_handler *cmd_handlers, size_t handlers_size);

/**
 * Parse and executes a command.
 *
 * If the command string contains criteria then the command will be executed on
 * all matching containers. Otherwise, it'll run on the `container` container. If
 * `container` is NULL then it'll run on the currently focused container.
 */
list_t *execute_command(char *command,  struct wmiiv_seat *seat,
		struct wmiiv_container *container);
/**
 * Parse and handles a command during config file loading.
 *
 * Do not use this under normal conditions.
 */
struct cmd_results *config_command(char *command, char **new_block);
/**
 * Parse and handle a sub command
 */
struct cmd_results *config_subcommand(char **argv, int argc,
		const struct cmd_handler *handlers, size_t handlers_size);
/*
 * Parses a command policy rule.
 */
struct cmd_results *config_commands_command(char *exec);
/**
 * Allocates a cmd_results object.
 */
struct cmd_results *cmd_results_new(enum cmd_status status, const char *error, ...);
/**
 * Frees a cmd_results object.
 */
void free_cmd_results(struct cmd_results *results);
/**
 * Serializes a list of cmd_results to a JSON string.
 *
 * Free the JSON string later on.
 */
char *cmd_results_to_json(list_t *res_list);

/**
 * TODO: Move this function and its dependent functions to container.c.
 */
void container_resize_tiled(struct wmiiv_container *parent, uint32_t axis,
		int amount);

struct wmiiv_container *container_find_resize_parent(struct wmiiv_container *container,
		uint32_t edge);

/**
 * Handlers shared by exec and exec_always.
 */
wmiiv_cmd cmd_exec_validate;
wmiiv_cmd cmd_exec_process;

wmiiv_cmd cmd_assign;
wmiiv_cmd cmd_bar;
wmiiv_cmd cmd_bindcode;
wmiiv_cmd cmd_bindswitch;
wmiiv_cmd cmd_bindsym;
wmiiv_cmd cmd_border;
wmiiv_cmd cmd_client_noop;
wmiiv_cmd cmd_client_focused;
wmiiv_cmd cmd_client_focused_inactive;
wmiiv_cmd cmd_client_focused_tab_title;
wmiiv_cmd cmd_client_unfocused;
wmiiv_cmd cmd_client_urgent;
wmiiv_cmd cmd_client_placeholder;
wmiiv_cmd cmd_client_background;
wmiiv_cmd cmd_commands;
wmiiv_cmd cmd_create_output;
wmiiv_cmd cmd_default_border;
wmiiv_cmd cmd_default_floating_border;
wmiiv_cmd cmd_exec;
wmiiv_cmd cmd_exec_always;
wmiiv_cmd cmd_exit;
wmiiv_cmd cmd_floating;
wmiiv_cmd cmd_floating_maximum_size;
wmiiv_cmd cmd_floating_minimum_size;
wmiiv_cmd cmd_floating_modifier;
wmiiv_cmd cmd_floating_scroll;
wmiiv_cmd cmd_focus;
wmiiv_cmd cmd_focus_follows_mouse;
wmiiv_cmd cmd_focus_on_window_activation;
wmiiv_cmd cmd_focus_wrapping;
wmiiv_cmd cmd_font;
wmiiv_cmd cmd_for_window;
wmiiv_cmd cmd_force_display_urgency_hint;
wmiiv_cmd cmd_force_focus_wrapping;
wmiiv_cmd cmd_fullscreen;
wmiiv_cmd cmd_gaps;
wmiiv_cmd cmd_hide_edge_borders;
wmiiv_cmd cmd_include;
wmiiv_cmd cmd_inhibit_idle;
wmiiv_cmd cmd_input;
wmiiv_cmd cmd_seat;
wmiiv_cmd cmd_ipc;
wmiiv_cmd cmd_kill;
wmiiv_cmd cmd_layout;
wmiiv_cmd cmd_log_colors;
wmiiv_cmd cmd_mark;
wmiiv_cmd cmd_max_render_time;
wmiiv_cmd cmd_mode;
wmiiv_cmd cmd_mouse_warping;
wmiiv_cmd cmd_move;
wmiiv_cmd cmd_new_float;
wmiiv_cmd cmd_new_window;
wmiiv_cmd cmd_nop;
wmiiv_cmd cmd_opacity;
wmiiv_cmd cmd_new_float;
wmiiv_cmd cmd_new_window;
wmiiv_cmd cmd_no_focus;
wmiiv_cmd cmd_output;
wmiiv_cmd cmd_permit;
wmiiv_cmd cmd_popup_during_fullscreen;
wmiiv_cmd cmd_reject;
wmiiv_cmd cmd_reload;
wmiiv_cmd cmd_rename;
wmiiv_cmd cmd_resize;
wmiiv_cmd cmd_seamless_mouse;
wmiiv_cmd cmd_set;
wmiiv_cmd cmd_shortcuts_inhibitor;
wmiiv_cmd cmd_show_marks;
wmiiv_cmd cmd_smart_borders;
wmiiv_cmd cmd_smart_gaps;
wmiiv_cmd cmd_split;
wmiiv_cmd cmd_splith;
wmiiv_cmd cmd_splitt;
wmiiv_cmd cmd_splitv;
wmiiv_cmd cmd_sticky;
wmiiv_cmd cmd_wmiivbg_command;
wmiiv_cmd cmd_wmiivnag_command;
wmiiv_cmd cmd_swap;
wmiiv_cmd cmd_tiling_drag;
wmiiv_cmd cmd_tiling_drag_threshold;
wmiiv_cmd cmd_title_align;
wmiiv_cmd cmd_title_format;
wmiiv_cmd cmd_titlebar_border_thickness;
wmiiv_cmd cmd_titlebar_padding;
wmiiv_cmd cmd_unbindcode;
wmiiv_cmd cmd_unbindswitch;
wmiiv_cmd cmd_unbindsym;
wmiiv_cmd cmd_unmark;
wmiiv_cmd cmd_urgent;
wmiiv_cmd cmd_workspace;
wmiiv_cmd cmd_workspace_layout;
wmiiv_cmd cmd_ws_auto_back_and_forth;
wmiiv_cmd cmd_xwayland;

wmiiv_cmd bar_cmd_bindcode;
wmiiv_cmd bar_cmd_binding_mode_indicator;
wmiiv_cmd bar_cmd_bindsym;
wmiiv_cmd bar_cmd_colors;
wmiiv_cmd bar_cmd_font;
wmiiv_cmd bar_cmd_gaps;
wmiiv_cmd bar_cmd_mode;
wmiiv_cmd bar_cmd_modifier;
wmiiv_cmd bar_cmd_output;
wmiiv_cmd bar_cmd_height;
wmiiv_cmd bar_cmd_hidden_state;
wmiiv_cmd bar_cmd_icon_theme;
wmiiv_cmd bar_cmd_id;
wmiiv_cmd bar_cmd_position;
wmiiv_cmd bar_cmd_separator_symbol;
wmiiv_cmd bar_cmd_status_command;
wmiiv_cmd bar_cmd_status_edge_padding;
wmiiv_cmd bar_cmd_status_padding;
wmiiv_cmd bar_cmd_pango_markup;
wmiiv_cmd bar_cmd_strip_workspace_numbers;
wmiiv_cmd bar_cmd_strip_workspace_name;
wmiiv_cmd bar_cmd_wmiivbar_command;
wmiiv_cmd bar_cmd_tray_bindcode;
wmiiv_cmd bar_cmd_tray_bindsym;
wmiiv_cmd bar_cmd_tray_output;
wmiiv_cmd bar_cmd_tray_padding;
wmiiv_cmd bar_cmd_unbindcode;
wmiiv_cmd bar_cmd_unbindsym;
wmiiv_cmd bar_cmd_wrap_scroll;
wmiiv_cmd bar_cmd_workspace_buttons;
wmiiv_cmd bar_cmd_workspace_min_width;

wmiiv_cmd bar_colors_cmd_active_workspace;
wmiiv_cmd bar_colors_cmd_background;
wmiiv_cmd bar_colors_cmd_focused_background;
wmiiv_cmd bar_colors_cmd_binding_mode;
wmiiv_cmd bar_colors_cmd_focused_workspace;
wmiiv_cmd bar_colors_cmd_inactive_workspace;
wmiiv_cmd bar_colors_cmd_separator;
wmiiv_cmd bar_colors_cmd_focused_separator;
wmiiv_cmd bar_colors_cmd_statusline;
wmiiv_cmd bar_colors_cmd_focused_statusline;
wmiiv_cmd bar_colors_cmd_urgent_workspace;

wmiiv_cmd input_cmd_seat;
wmiiv_cmd input_cmd_accel_profile;
wmiiv_cmd input_cmd_calibration_matrix;
wmiiv_cmd input_cmd_click_method;
wmiiv_cmd input_cmd_drag;
wmiiv_cmd input_cmd_drag_lock;
wmiiv_cmd input_cmd_dwt;
wmiiv_cmd input_cmd_events;
wmiiv_cmd input_cmd_left_handed;
wmiiv_cmd input_cmd_map_from_region;
wmiiv_cmd input_cmd_map_to_output;
wmiiv_cmd input_cmd_map_to_region;
wmiiv_cmd input_cmd_middle_emulation;
wmiiv_cmd input_cmd_natural_scroll;
wmiiv_cmd input_cmd_pointer_accel;
wmiiv_cmd input_cmd_scroll_factor;
wmiiv_cmd input_cmd_repeat_delay;
wmiiv_cmd input_cmd_repeat_rate;
wmiiv_cmd input_cmd_scroll_button;
wmiiv_cmd input_cmd_scroll_method;
wmiiv_cmd input_cmd_tap;
wmiiv_cmd input_cmd_tap_button_map;
wmiiv_cmd input_cmd_tool_mode;
wmiiv_cmd input_cmd_xkb_capslock;
wmiiv_cmd input_cmd_xkb_file;
wmiiv_cmd input_cmd_xkb_layout;
wmiiv_cmd input_cmd_xkb_model;
wmiiv_cmd input_cmd_xkb_numlock;
wmiiv_cmd input_cmd_xkb_options;
wmiiv_cmd input_cmd_xkb_rules;
wmiiv_cmd input_cmd_xkb_switch_layout;
wmiiv_cmd input_cmd_xkb_variant;

wmiiv_cmd output_cmd_adaptive_sync;
wmiiv_cmd output_cmd_background;
wmiiv_cmd output_cmd_disable;
wmiiv_cmd output_cmd_dpms;
wmiiv_cmd output_cmd_enable;
wmiiv_cmd output_cmd_max_render_time;
wmiiv_cmd output_cmd_mode;
wmiiv_cmd output_cmd_modeline;
wmiiv_cmd output_cmd_position;
wmiiv_cmd output_cmd_render_bit_depth;
wmiiv_cmd output_cmd_scale;
wmiiv_cmd output_cmd_scale_filter;
wmiiv_cmd output_cmd_subpixel;
wmiiv_cmd output_cmd_toggle;
wmiiv_cmd output_cmd_transform;

wmiiv_cmd seat_cmd_attach;
wmiiv_cmd seat_cmd_cursor;
wmiiv_cmd seat_cmd_fallback;
wmiiv_cmd seat_cmd_hide_cursor;
wmiiv_cmd seat_cmd_idle_inhibit;
wmiiv_cmd seat_cmd_idle_wake;
wmiiv_cmd seat_cmd_keyboard_grouping;
wmiiv_cmd seat_cmd_pointer_constraint;
wmiiv_cmd seat_cmd_shortcuts_inhibitor;
wmiiv_cmd seat_cmd_xcursor_theme;

wmiiv_cmd cmd_ipc_cmd;
wmiiv_cmd cmd_ipc_events;
wmiiv_cmd cmd_ipc_event_cmd;

#endif
