#define _POSIX_C_SOURCE 200809L
#include "hayward/scene/text.h"

#include <cairo.h>
#include <glib-object.h>
#include <glib/gmacros.h>
#include <glib/gtypes.h>
#include <math.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <hayward-common/cairo_util.h>
#include <hayward-common/pango.h>

#include <wayland-server-protocol.h>

#include <hayward/config.h>
#include <hayward/scene/cairo.h>
#include <hayward/scene/colours.h>

struct hwd_text_node_state {
    struct wlr_scene_node *node;

    // User specified properties.
    char *text;
    int max_width;
    bool pango_markup;
    PangoFontDescription *font_description;
    struct hwd_colour colour;

    // Calculated text properties in layout coordinates.
    int text_baseline;
    int text_width;
    int text_height;

    // Properties derived from set of active outputs.
    float scale;
    enum wl_output_subpixel subpixel;

    struct wl_list outputs; // hwd_text_node_output.link

    struct wl_listener output_enter;
    struct wl_listener output_leave;
    struct wl_listener destroy;
};

struct hwd_text_node_output {
    struct hwd_text_node_state *state;
    struct wl_list link;

    struct wlr_output *output;

    struct wl_listener commit;
};

static void
hwd_text_node_reshape(struct wlr_scene_node *node) {
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct hwd_text_node_state *state = node->data;

    int layout_width = state->text_width;
    if (state->max_width > 0) {
        layout_width = MIN(state->text_width, state->max_width);
    }
    int layout_height = state->text_height;

    struct wlr_fbox source_box = {
        .x = 0,
        .y = 0,
        .width = layout_width * state->scale,
        .height = layout_height * state->scale,
    };
    wlr_scene_buffer_set_source_box(scene_buffer, &source_box);

    wlr_scene_buffer_set_dest_size(scene_buffer, layout_width, layout_height);
}

static void
hwd_text_node_redraw(struct wlr_scene_node *node) {
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct hwd_text_node_state *state = node->data;

    cairo_t *c = cairo_create(NULL);
    cairo_set_antialias(c, CAIRO_ANTIALIAS_BEST);
    get_text_size(
        c, state->font_description, &state->text_width, NULL, NULL, 1, state->pango_markup, "%s",
        state->text
    );
    cairo_destroy(c);
    get_text_metrics(state->font_description, &state->text_height, &state->text_baseline);

    int buffer_width = ceil(state->text_width * state->scale);
    int buffer_height = ceil(state->text_height * state->scale);

    struct hwd_colour colour = state->colour;
    PangoContext *pango = NULL;

    struct wlr_buffer *buffer = scene_buffer->buffer;
    if (buffer == NULL || buffer->width < buffer_width || buffer->height < buffer_height) {
        wlr_buffer_drop(buffer);
        buffer = hwd_cairo_buffer_create(buffer_width, buffer_height);
        wlr_scene_buffer_set_buffer(scene_buffer, buffer);
    }

    cairo_t *cairo = hwd_cairo_buffer_get_context(buffer);

    cairo_font_options_t *fo = cairo_font_options_create();
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
    enum wl_output_subpixel subpixel = state->subpixel;
    if (subpixel == WL_OUTPUT_SUBPIXEL_NONE || subpixel == WL_OUTPUT_SUBPIXEL_UNKNOWN) {
        cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
    } else {
        cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
        cairo_font_options_set_subpixel_order(fo, to_cairo_subpixel_order(subpixel));
    }
    cairo_set_font_options(cairo, fo);
    pango = pango_cairo_create_context(cairo);

    cairo_set_source_rgba(cairo, colour.r, colour.g, colour.b, colour.a);
    cairo_move_to(cairo, 0, (config->font_baseline - state->text_baseline) * state->scale);
    render_text(
        cairo, state->font_description, state->scale, state->pango_markup, "%s", state->text
    );

    cairo_surface_flush(cairo_get_target(cairo));

    hwd_text_node_reshape(node);

    g_clear_object(&pango);
    cairo_font_options_destroy(fo);
}

static void
hwd_text_node_reindex_outputs(struct wlr_scene_node *node) {
    struct hwd_text_node_state *state = node->data;

    float scale = 0;
    enum wl_output_subpixel subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;

    struct hwd_text_node_output *output;
    wl_list_for_each(output, &state->outputs, link) {
        if (subpixel == WL_OUTPUT_SUBPIXEL_UNKNOWN) {
            subpixel = output->output->subpixel;
        } else if (subpixel != output->output->subpixel) {
            subpixel = WL_OUTPUT_SUBPIXEL_NONE;
        }

        if (scale != 0 && scale != output->output->scale) {
            // drop down to gray scale if we encounter outputs with different
            // scales or else we will have chromatic aberations
            subpixel = WL_OUTPUT_SUBPIXEL_NONE;
        }

        if (scale < output->output->scale) {
            scale = output->output->scale;
        }
    }

    // no outputs
    if (scale == 0) {
        return;
    }

    if (scale != state->scale || subpixel != state->subpixel) {
        state->scale = scale;
        state->subpixel = subpixel;
        hwd_text_node_redraw(node);
    }
}

static void
hwd_text_node_handle_output_commit(struct wl_listener *listener, void *data) {
    struct hwd_text_node_output *output = wl_container_of(listener, output, commit);
    struct wlr_output_event_commit *event = data;

    if (event->state->committed & (WLR_OUTPUT_STATE_SCALE | WLR_OUTPUT_STATE_SUBPIXEL)) {
        hwd_text_node_reindex_outputs(output->state->node);
    }
}

static void
hwd_text_node_handle_output_enter(struct wl_listener *listener, void *data) {
    struct hwd_text_node_state *state = wl_container_of(listener, state, output_enter);
    struct wlr_scene_output *output = data;
    struct hwd_text_node_output *buffer_output = calloc(1, sizeof(*buffer_output));
    if (!buffer_output) {
        return;
    }

    buffer_output->state = state;

    buffer_output->commit.notify = hwd_text_node_handle_output_commit;
    wl_signal_add(&output->output->events.commit, &buffer_output->commit);

    buffer_output->output = output->output;
    wl_list_insert(&state->outputs, &buffer_output->link);
    hwd_text_node_reindex_outputs(state->node);
}

static void
hwd_text_node_output_destroy(struct hwd_text_node_output *output) {
    if (!output) {
        return;
    }

    wl_list_remove(&output->link);
    wl_list_remove(&output->commit.link);
    free(output);
}

static void
hwd_text_node_handle_output_leave(struct wl_listener *listener, void *data) {
    struct hwd_text_node_state *state = wl_container_of(listener, state, output_leave);
    struct wlr_scene_output *scene_output = data;

    struct hwd_text_node_output *output, *tmp_output;
    wl_list_for_each_safe(output, tmp_output, &state->outputs, link) {
        if (output->output == scene_output->output) {
            hwd_text_node_output_destroy(output);
        }
    }

    hwd_text_node_reindex_outputs(state->node);
}

static void
hwd_text_node_handle_destroy(struct wl_listener *listener, void *data) {
    struct hwd_text_node_state *state = wl_container_of(listener, state, destroy);

    wl_list_remove(&state->output_enter.link);
    wl_list_remove(&state->output_leave.link);
    wl_list_remove(&state->destroy.link);

    struct hwd_text_node_output *output, *tmp_output;
    wl_list_for_each_safe(output, tmp_output, &state->outputs, link) {
        hwd_text_node_output_destroy(output);
    }

    free(state->text);
    free(state);
}

struct wlr_scene_node *
hwd_text_node_create(
    struct wlr_scene_tree *parent, char *text, struct hwd_colour colour, bool pango_markup,
    PangoFontDescription *font_description
) {

    struct hwd_text_node_state *state = calloc(1, sizeof(struct hwd_text_node_state));
    if (state == NULL) {
        return NULL;
    }

    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_create(parent, NULL);
    if (scene_buffer == NULL) {
        free(state);
        return NULL;
    }
    struct wlr_scene_node *node = &scene_buffer->node;
    node->data = state;

    state->node = node;
    state->text = strdup(text);
    if (state->text == NULL) {
        wlr_scene_node_destroy(node);
        free(state);
        return NULL;
    }
    state->max_width = 0;
    state->pango_markup = pango_markup;
    state->font_description = font_description;
    state->colour = colour;

    wl_list_init(&state->outputs);
    state->scale = 1.0;
    state->subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;

    state->destroy.notify = hwd_text_node_handle_destroy;
    wl_signal_add(&node->events.destroy, &state->destroy);
    state->output_enter.notify = hwd_text_node_handle_output_enter;
    wl_signal_add(&scene_buffer->events.output_enter, &state->output_enter);
    state->output_leave.notify = hwd_text_node_handle_output_leave;
    wl_signal_add(&scene_buffer->events.output_leave, &state->output_leave);

    return node;
}

void
hwd_text_node_set_color(struct wlr_scene_node *node, struct hwd_colour colour) {
    struct hwd_text_node_state *state = node->data;

    if (memcmp(&state->colour, &colour, sizeof(struct hwd_colour)) == 0) {
        return;
    }
    state->colour = colour;
    hwd_text_node_redraw(node);
}

void
hwd_text_node_set_text(struct wlr_scene_node *node, char *text) {
    struct hwd_text_node_state *state = node->data;

    if (strcmp(state->text, text) == 0) {
        return;
    }

    char *new_text = strdup(text);
    if (!new_text) {
        return;
    }

    free(state->text);
    state->text = new_text;

    hwd_text_node_redraw(node);
}

void
hwd_text_node_set_max_width(struct wlr_scene_node *node, int max_width) {
    struct hwd_text_node_state *state = node->data;

    state->max_width = max_width;
    hwd_text_node_reshape(node);
}
