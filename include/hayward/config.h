#ifndef HWD_CONFIG_H
#define HWD_CONFIG_H

#include <config.h>

#include <pango/pango.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

#include <wlr/types/wlr_switch.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/util/box.h>

#include <hayward/haywardnag.h>
#include <hayward/input/tablet.h>
#include <hayward/list.h>

// TODO: Refactor this shit

struct hwd_window;
struct hwd_column;

/**
 * Describes a variable created via the `set` command.
 */
struct hwd_variable {
    char *name;
    char *value;
};

enum binding_input_type {
    BINDING_KEYCODE,
    BINDING_KEYSYM,
    BINDING_MOUSECODE,
    BINDING_MOUSESYM,
    BINDING_SWITCH
};

enum binding_flags {
    BINDING_RELEASE = 1 << 0,
    BINDING_LOCKED = 1 << 1,    // keyboard only
    BINDING_BORDER = 1 << 2,    // mouse only; trigger on container border
    BINDING_CONTENTS = 1 << 3,  // mouse only; trigger on container contents
    BINDING_TITLEBAR = 1 << 4,  // mouse only; trigger on container titlebar
    BINDING_CODE = 1 << 5,      // keyboard only; convert keysyms into keycodes
    BINDING_RELOAD = 1 << 6,    // switch only; (re)trigger binding on reload
    BINDING_INHIBITED = 1 << 7, // keyboard only: ignore shortcut inhibitor
    BINDING_NOREPEAT = 1 << 8,  // keyboard only; do not trigger when repeating a held key
};

/**
 * A key (or mouse) binding and an associated command.
 */
struct hwd_binding {
    enum binding_input_type type;
    int order;
    char *input;
    uint32_t flags;
    list_t *keys; // sorted in ascending order
    list_t *syms; // sorted in ascending order; NULL if BINDING_CODE is not set
    uint32_t modifiers;
    xkb_layout_index_t group;
    char *command;
};

enum hwd_switch_trigger {
    HWD_SWITCH_TRIGGER_OFF,
    HWD_SWITCH_TRIGGER_ON,
    HWD_SWITCH_TRIGGER_TOGGLE,
};

/**
 * A laptop switch binding and an associated command.
 */
struct hwd_switch_binding {
    enum wlr_switch_type type;
    enum hwd_switch_trigger trigger;
    uint32_t flags;
    char *command;
};

/**
 * Focus on window activation.
 */
enum hwd_fowa {
    FOWA_SMART,
    FOWA_URGENT,
    FOWA_FOCUS,
    FOWA_NONE,
};

/**
 * A "mode" of keybindings created via the `mode` command.
 */
struct hwd_mode {
    char *name;
    list_t *keysym_bindings;
    list_t *keycode_bindings;
    list_t *mouse_bindings;
    list_t *switch_bindings;
    bool pango;
};

struct input_config_mapped_from_region {
    double x1, y1;
    double x2, y2;
    bool mm;
};

struct calibration_matrix {
    bool configured;
    float matrix[6];
};

enum input_config_mapped_to {
    MAPPED_TO_DEFAULT,
    MAPPED_TO_OUTPUT,
    MAPPED_TO_REGION,
};

struct input_config_tool {
    enum wlr_tablet_tool_type type;
    enum hwd_tablet_tool_mode mode;
};

/**
 * options for input devices
 */
struct input_config {
    char *identifier;
    const char *input_type;

    int accel_profile;
    struct calibration_matrix calibration_matrix;
    int click_method;
    int drag;
    int drag_lock;
    int dwt;
    int left_handed;
    int middle_emulation;
    int natural_scroll;
    float pointer_accel;
    float scroll_factor;
    int repeat_delay;
    int repeat_rate;
    int scroll_button;
    int scroll_method;
    int send_events;
    int tap;
    int tap_button_map;

    char *xkb_layout;
    char *xkb_model;
    char *xkb_options;
    char *xkb_rules;
    char *xkb_variant;
    char *xkb_file;

    bool xkb_file_is_set;

    int xkb_numlock;
    int xkb_capslock;

    struct input_config_mapped_from_region *mapped_from_region;

    enum input_config_mapped_to mapped_to;
    char *mapped_to_output;
    struct wlr_box *mapped_to_region;

    list_t *tools;

    bool capturable;
    struct wlr_box region;
};

/**
 * Options for misc device configurations that happen in the seat block
 */
struct seat_attachment_config {
    char *identifier;
    // TODO other things are configured here for some reason
};

enum seat_config_hide_cursor_when_typing {
    HIDE_WHEN_TYPING_DEFAULT, // the default is currently disabled
    HIDE_WHEN_TYPING_ENABLE,
    HIDE_WHEN_TYPING_DISABLE,
};

enum seat_config_allow_constrain {
    CONSTRAIN_DEFAULT, // the default is currently enabled
    CONSTRAIN_ENABLE,
    CONSTRAIN_DISABLE,
};

enum seat_config_shortcuts_inhibit {
    SHORTCUTS_INHIBIT_DEFAULT, // the default is currently enabled
    SHORTCUTS_INHIBIT_ENABLE,
    SHORTCUTS_INHIBIT_DISABLE,
};

enum seat_keyboard_grouping {
    KEYBOARD_GROUP_DEFAULT, // the default is currently smart
    KEYBOARD_GROUP_NONE,
    KEYBOARD_GROUP_SMART, // keymap and repeat info
};

enum hwd_input_idle_source {
    IDLE_SOURCE_KEYBOARD = 1 << 0,
    IDLE_SOURCE_POINTER = 1 << 1,
    IDLE_SOURCE_TOUCH = 1 << 2,
    IDLE_SOURCE_TABLET_PAD = 1 << 3,
    IDLE_SOURCE_TABLET_TOOL = 1 << 4,
    IDLE_SOURCE_SWITCH = 1 << 5,
};

/**
 * Options for multiseat and other misc device configurations
 */
struct seat_config {
    char *name;
    int fallback;        // -1 means not set
    list_t *attachments; // list of seat_attachment configs
    int hide_cursor_timeout;
    enum seat_config_hide_cursor_when_typing hide_cursor_when_typing;
    enum seat_config_allow_constrain allow_constrain;
    enum seat_config_shortcuts_inhibit shortcuts_inhibit;
    enum seat_keyboard_grouping keyboard_grouping;
    uint32_t idle_inhibit_sources, idle_wake_sources;
    struct {
        char *name;
        int size;
    } xcursor_theme;
};

enum focus_follows_mouse_mode {
    FOLLOWS_NO,
    FOLLOWS_YES,
    FOLLOWS_ALWAYS,
};

enum focus_wrapping_mode {
    WRAP_NO,
    WRAP_YES,
    WRAP_FORCE,
    WRAP_WORKSPACE,
};

enum xwayland_mode {
    XWAYLAND_MODE_DISABLED,
    XWAYLAND_MODE_LAZY,
    XWAYLAND_MODE_IMMEDIATE,
};

/**
 * The configuration struct. The result of loading a config file.
 */
struct hwd_config {
    char *haywardnag_command;
    struct haywardnag_instance haywardnag_config_errors;
    list_t *symbols;
    list_t *modes;
    list_t *cmd_queue;
    list_t *output_configs;
    list_t *input_configs;
    list_t *input_type_configs;
    list_t *seat_configs;
    list_t *criteria;
    list_t *no_focus;
    struct hwd_mode *current_mode;
    uint32_t floating_mod;
    bool floating_mod_inverse;
    char *font;                             // Used for IPC.
    PangoFontDescription *font_description; // Used internally for rendering and validating.
    int font_height;
    int font_baseline;
    size_t urgent_timeout;
    enum hwd_fowa focus_on_window_activation;
    enum xwayland_mode xwayland;

    // Flags
    enum focus_follows_mouse_mode focus_follows_mouse;
    enum focus_wrapping_mode focus_wrapping;
    bool active;
    bool failed;
    bool reloading;
    bool reading;
    bool validating;
    bool show_marks;

    int tiling_drag_threshold;

    list_t *config_chain;
    bool user_config_path;
    const char *current_config_path;
    const char *current_config;
    int current_config_line_number;
    char *current_config_line;

    bool has_focused_tab_title;

    // floating view
    int32_t floating_maximum_width;
    int32_t floating_maximum_height;
    int32_t floating_minimum_width;
    int32_t floating_minimum_height;

    // The keysym to keycode translation
    struct xkb_state *keysym_translation_state;

    // Context for command handlers
    struct {
        struct input_config *input_config;
        struct seat_config *seat_config;
        struct hwd_seat *seat;
        struct hwd_workspace *workspace;
        struct hwd_window *window;
        struct {
            int argc;
            char **argv;
        } leftovers;
    } handler_context;
};

/**
 * Loads the main config from the given path. is_active should be true when
 * reloading the config.
 */
bool
load_main_config(const char *path, bool is_active, bool validating);

/**
 * Loads an included config. Can only be used after load_main_config.
 */
void
load_include_configs(
    const char *path, struct hwd_config *config, struct haywardnag_instance *haywardnag
);

/**
 * Run the commands that were deferred when reading the config file.
 */
void
run_deferred_commands(void);

/**
 * Run the binding commands that were deferred when initializing the inputs
 */
void
run_deferred_bindings(void);

/**
 * Adds a warning entry to the haywardnag instance used for errors.
 */
void
config_add_haywardnag_warning(char *fmt, ...);

/**
 * Free config struct
 */
void
free_config(struct hwd_config *config);

void
free_hwd_variable(struct hwd_variable *var);

/**
 * Does variable replacement for a string based on the config's currently loaded
 * variables.
 */
char *
do_var_replacement(char *str);

struct input_config *
new_input_config(const char *identifier);

void
merge_input_config(struct input_config *dst, struct input_config *src);

struct input_config *
store_input_config(struct input_config *ic, char **error);

void
input_config_fill_rule_names(struct input_config *ic, struct xkb_rule_names *rules);

void
free_input_config(struct input_config *ic);

int
seat_name_cmp(const void *item, const void *data);

struct seat_config *
new_seat_config(const char *name);

void
free_seat_config(struct seat_config *ic);

struct seat_attachment_config *
seat_attachment_config_new(void);

struct seat_attachment_config *
seat_config_get_attachment(struct seat_config *seat_config, char *identifier);

struct seat_config *
store_seat_config(struct seat_config *seat);

void
free_hwd_binding(struct hwd_binding *sb);

void
free_switch_binding(struct hwd_switch_binding *binding);

void
seat_execute_command(struct hwd_seat *seat, struct hwd_binding *binding);

/**
 * Updates the value of config->font_height based on the metrics for title's
 * font as reported by pango.
 *
 * If the height has changed, all containers will be rearranged to take on the
 * new size.
 */
void
config_update_font_height(void);

/**
 * Convert bindsym into bindcode using the first configured layout.
 * Return false in case the conversion is unsuccessful.
 */
bool
translate_binding(struct hwd_binding *binding);

void
translate_keysyms(struct input_config *input_config);

void
binding_add_translated(struct hwd_binding *binding, list_t *bindings);

/* Global config singleton. */
extern struct hwd_config *config;

#endif
