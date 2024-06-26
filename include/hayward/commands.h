#ifndef HWD_COMMANDS_H
#define HWD_COMMANDS_H

#include <stddef.h>
#include <stdint.h>

#include <hayward/config.h>
#include <hayward/input/seat.h>
#include <hayward/list.h>

struct hwd_window;

typedef struct cmd_results *
hwd_cmd(int argc, char **argv);

struct cmd_handler {
    char *command;
    hwd_cmd *handle;
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
find_handler(char *line, const struct cmd_handler *cmd_handlers, size_t handlers_size);

/**
 * Parse and executes a command.
 *
 * If the command string contains criteria then the command will be executed on
 * all matching containers. Otherwise, it'll run on the `container` container.
 * If `container` is NULL then it'll run on the currently focused container.
 */
list_t *
execute_command(char *command, struct hwd_seat *seat, struct hwd_window *container);
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
config_subcommand(char **argv, int argc, const struct cmd_handler *handlers, size_t handlers_size);
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

void
window_resize_tiled(struct hwd_window *window, uint32_t axis, int amount);

/**
 * Handlers shared by exec and exec_always.
 */
hwd_cmd cmd_exec_validate;
hwd_cmd cmd_exec_process;

hwd_cmd cmd_bindcode;
hwd_cmd cmd_bindswitch;
hwd_cmd cmd_bindsym;
hwd_cmd cmd_exec;
hwd_cmd cmd_exec_always;
hwd_cmd cmd_exit;
hwd_cmd cmd_floating;
hwd_cmd cmd_floating_maximum_size;
hwd_cmd cmd_floating_minimum_size;
hwd_cmd cmd_floating_modifier;
hwd_cmd cmd_focus;
hwd_cmd cmd_focus_follows_mouse;
hwd_cmd cmd_focus_on_window_activation;
hwd_cmd cmd_focus_wrapping;
hwd_cmd cmd_font;
hwd_cmd cmd_force_display_urgency_hint;
hwd_cmd cmd_fullscreen;
hwd_cmd cmd_haywardnag_command;
hwd_cmd cmd_include;
hwd_cmd cmd_input;
hwd_cmd cmd_kill;
hwd_cmd cmd_layout;
hwd_cmd cmd_mode;
hwd_cmd cmd_move;
hwd_cmd cmd_nop;
hwd_cmd cmd_reload;
hwd_cmd cmd_resize;
hwd_cmd cmd_seat;
hwd_cmd cmd_set;
hwd_cmd cmd_tiling_drag_threshold;
hwd_cmd cmd_unbindcode;
hwd_cmd cmd_unbindswitch;
hwd_cmd cmd_unbindsym;
hwd_cmd cmd_workspace;
hwd_cmd cmd_xwayland;

hwd_cmd input_cmd_accel_profile;
hwd_cmd input_cmd_calibration_matrix;
hwd_cmd input_cmd_click_method;
hwd_cmd input_cmd_drag;
hwd_cmd input_cmd_drag_lock;
hwd_cmd input_cmd_dwt;
hwd_cmd input_cmd_events;
hwd_cmd input_cmd_left_handed;
hwd_cmd input_cmd_map_from_region;
hwd_cmd input_cmd_map_to_output;
hwd_cmd input_cmd_map_to_region;
hwd_cmd input_cmd_middle_emulation;
hwd_cmd input_cmd_natural_scroll;
hwd_cmd input_cmd_pointer_accel;
hwd_cmd input_cmd_repeat_delay;
hwd_cmd input_cmd_repeat_rate;
hwd_cmd input_cmd_scroll_button;
hwd_cmd input_cmd_scroll_factor;
hwd_cmd input_cmd_scroll_method;
hwd_cmd input_cmd_tap;
hwd_cmd input_cmd_tap_button_map;
hwd_cmd input_cmd_tool_mode;
hwd_cmd input_cmd_xkb_capslock;
hwd_cmd input_cmd_xkb_file;
hwd_cmd input_cmd_xkb_layout;
hwd_cmd input_cmd_xkb_model;
hwd_cmd input_cmd_xkb_numlock;
hwd_cmd input_cmd_xkb_options;
hwd_cmd input_cmd_xkb_rules;
hwd_cmd input_cmd_xkb_switch_layout;
hwd_cmd input_cmd_xkb_variant;

hwd_cmd seat_cmd_attach;
hwd_cmd seat_cmd_fallback;
hwd_cmd seat_cmd_hide_cursor;
hwd_cmd seat_cmd_idle_inhibit;
hwd_cmd seat_cmd_idle_wake;
hwd_cmd seat_cmd_keyboard_grouping;
hwd_cmd seat_cmd_pointer_constraint;
hwd_cmd seat_cmd_xcursor_theme;

#endif
