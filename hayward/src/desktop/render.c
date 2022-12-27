#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <GLES2/gl2.h>
#include <assert.h>
#include <math.h>
#include <pixman.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <wayland-util.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_damage.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/region.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <wayland-server-protocol.h>
#include <wlr-layer-shell-unstable-v1-protocol.h>

#include <hayward/config.h>
#include <hayward/input/input-manager.h>
#include <hayward/input/seat.h>
#include <hayward/output.h>
#include <hayward/server.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

#include <config.h>

struct render_data {
    pixman_region32_t *damage;
    float alpha;
    struct wlr_box *clip_box;
};

/**
 * Apply scale to a width or height.
 *
 * One does not simply multiply the width by the scale. We allow fractional
 * scaling, which means the resulting scaled width might be a decimal.
 * So we round it.
 *
 * But even this can produce undesirable results depending on the X or Y offset
 * of the box. For example, with a scale of 1.5, a box with width=1 should not
 * scale to 2px if its X coordinate is 1, because the X coordinate would have
 * scaled to 2px.
 */
static int
scale_length(int length, int offset, float scale) {
    return round((offset + length) * scale) - round(offset * scale);
}

static void
scissor_output(struct wlr_output *wlr_output, pixman_box32_t *rect) {
    struct wlr_renderer *renderer = wlr_output->renderer;
    assert(renderer);

    struct wlr_box box = {
        .x = rect->x1,
        .y = rect->y1,
        .width = rect->x2 - rect->x1,
        .height = rect->y2 - rect->y1,
    };

    int ow, oh;
    wlr_output_transformed_resolution(wlr_output, &ow, &oh);

    enum wl_output_transform transform =
        wlr_output_transform_invert(wlr_output->transform);
    wlr_box_transform(&box, &box, transform, ow, oh);

    wlr_renderer_scissor(renderer, &box);
}

static void
set_scale_filter(
    struct wlr_output *wlr_output, struct wlr_texture *texture,
    enum scale_filter_mode scale_filter
) {
    if (!wlr_texture_is_gles2(texture)) {
        return;
    }

    struct wlr_gles2_texture_attribs attribs;
    wlr_gles2_texture_get_attribs(texture, &attribs);

    glBindTexture(attribs.target, attribs.tex);

    switch (scale_filter) {
    case SCALE_FILTER_LINEAR:
        glTexParameteri(attribs.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        break;
    case SCALE_FILTER_NEAREST:
        glTexParameteri(attribs.target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        break;
    case SCALE_FILTER_DEFAULT:
    case SCALE_FILTER_SMART:
        assert(false); // unreachable
    }
}

static void
render_texture(
    struct wlr_output *wlr_output, pixman_region32_t *output_damage,
    struct wlr_texture *texture, const struct wlr_fbox *src_box,
    const struct wlr_box *dst_box, const float matrix[static 9], float alpha
) {
    struct wlr_renderer *renderer = wlr_output->renderer;
    struct hayward_output *output = wlr_output->data;

    pixman_region32_t damage;
    pixman_region32_init(&damage);
    pixman_region32_union_rect(
        &damage, &damage, dst_box->x, dst_box->y, dst_box->width,
        dst_box->height
    );
    pixman_region32_intersect(&damage, &damage, output_damage);
    bool damaged = pixman_region32_not_empty(&damage);
    if (!damaged) {
        goto damage_finish;
    }

    int nrects;
    pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
    for (int i = 0; i < nrects; ++i) {
        scissor_output(wlr_output, &rects[i]);
        set_scale_filter(wlr_output, texture, output->scale_filter);
        if (src_box != NULL) {
            wlr_render_subtexture_with_matrix(
                renderer, texture, src_box, matrix, alpha
            );
        } else {
            wlr_render_texture_with_matrix(renderer, texture, matrix, alpha);
        }
    }

damage_finish:
    pixman_region32_fini(&damage);
}

static void
render_surface_iterator(
    struct hayward_output *output, struct hayward_view *view,
    struct wlr_surface *surface, struct wlr_box *_box, void *_data
) {
    struct render_data *data = _data;
    struct wlr_output *wlr_output = output->wlr_output;
    pixman_region32_t *output_damage = data->damage;
    float alpha = data->alpha;

    struct wlr_texture *texture = wlr_surface_get_texture(surface);
    if (!texture) {
        return;
    }

    struct wlr_fbox src_box;
    wlr_surface_get_buffer_source_box(surface, &src_box);

    struct wlr_box proj_box = *_box;
    scale_box(&proj_box, wlr_output->scale);

    float matrix[9];
    enum wl_output_transform transform =
        wlr_output_transform_invert(surface->current.transform);
    wlr_matrix_project_box(
        matrix, &proj_box, transform, 0.0, wlr_output->transform_matrix
    );

    struct wlr_box dst_box = *_box;
    struct wlr_box *clip_box = data->clip_box;
    if (clip_box != NULL) {
        dst_box.width = fmin(dst_box.width, clip_box->width);
        dst_box.height = fmin(dst_box.height, clip_box->height);
    }
    scale_box(&dst_box, wlr_output->scale);

    render_texture(
        wlr_output, output_damage, texture, &src_box, &dst_box, matrix, alpha
    );

    wlr_presentation_surface_sampled_on_output(
        server.presentation, surface, wlr_output
    );
}

static void
render_layer_toplevel(
    struct hayward_output *output, pixman_region32_t *damage,
    struct wl_list *layer_surfaces
) {
    struct render_data data = {
        .damage = damage,
        .alpha = 1.0f,
    };
    output_layer_for_each_toplevel_surface(
        output, layer_surfaces, render_surface_iterator, &data
    );
}

static void
render_layer_popups(
    struct hayward_output *output, pixman_region32_t *damage,
    struct wl_list *layer_surfaces
) {
    struct render_data data = {
        .damage = damage,
        .alpha = 1.0f,
    };
    output_layer_for_each_popup_surface(
        output, layer_surfaces, render_surface_iterator, &data
    );
}

#if HAVE_XWAYLAND
static void
render_unmanaged(
    struct hayward_output *output, pixman_region32_t *damage,
    struct wl_list *unmanaged
) {
    struct render_data data = {
        .damage = damage,
        .alpha = 1.0f,
    };
    output_unmanaged_for_each_surface(
        output, unmanaged, render_surface_iterator, &data
    );
}
#endif

static void
render_drag_icons(
    struct hayward_output *output, pixman_region32_t *damage,
    struct wl_list *drag_icons
) {
    struct render_data data = {
        .damage = damage,
        .alpha = 1.0f,
    };
    output_drag_icons_for_each_surface(
        output, drag_icons, render_surface_iterator, &data
    );
}

// _box.x and .y are expected to be layout-local
// _box.width and .height are expected to be output-buffer-local
void
render_rect(
    struct hayward_output *output, pixman_region32_t *output_damage,
    const struct wlr_box *_box, float color[static 4]
) {
    struct wlr_output *wlr_output = output->wlr_output;
    struct wlr_renderer *renderer = wlr_output->renderer;

    struct wlr_box box;
    memcpy(&box, _box, sizeof(struct wlr_box));
    box.x -= output->lx * wlr_output->scale;
    box.y -= output->ly * wlr_output->scale;

    pixman_region32_t damage;
    pixman_region32_init(&damage);
    pixman_region32_union_rect(
        &damage, &damage, box.x, box.y, box.width, box.height
    );
    pixman_region32_intersect(&damage, &damage, output_damage);
    bool damaged = pixman_region32_not_empty(&damage);
    if (!damaged) {
        goto damage_finish;
    }

    int nrects;
    pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
    for (int i = 0; i < nrects; ++i) {
        scissor_output(wlr_output, &rects[i]);
        wlr_render_rect(renderer, &box, color, wlr_output->transform_matrix);
    }

damage_finish:
    pixman_region32_fini(&damage);
}

void
premultiply_alpha(float color[4], float opacity) {
    color[3] *= opacity;
    color[0] *= color[3];
    color[1] *= color[3];
    color[2] *= color[3];
}

static void
render_view_toplevels(
    struct hayward_view *view, struct hayward_output *output,
    pixman_region32_t *damage, float alpha
) {
    struct render_data data = {
        .damage = damage,
        .alpha = alpha,
    };
    struct wlr_box clip_box;
    if (!window_is_current_floating(view->window)) {
        // As we pass the geometry offsets to the surface iterator, we will
        // need to account for the offsets in the clip dimensions.
        clip_box.width = view->window->current.content_width + view->geometry.x;
        clip_box.height =
            view->window->current.content_height + view->geometry.y;
        data.clip_box = &clip_box;
    }
    // Render all toplevels without descending into popups
    double ox = view->window->surface_x - output->lx - view->geometry.x;
    double oy = view->window->surface_y - output->ly - view->geometry.y;
    output_surface_for_each_surface(
        output, view->surface, ox, oy, render_surface_iterator, &data
    );
}

static void
render_view_popups(
    struct hayward_view *view, struct hayward_output *output,
    pixman_region32_t *damage, float alpha
) {
    struct render_data data = {
        .damage = damage,
        .alpha = alpha,
    };
    output_view_for_each_popup_surface(
        output, view, render_surface_iterator, &data
    );
}

static void
render_saved_view(
    struct hayward_view *view, struct hayward_output *output,
    pixman_region32_t *damage, float alpha
) {
    struct wlr_output *wlr_output = output->wlr_output;

    if (wl_list_empty(&view->saved_buffers)) {
        return;
    }

    bool floating = window_is_current_floating(view->window);

    struct hayward_saved_buffer *saved_buf;
    wl_list_for_each(saved_buf, &view->saved_buffers, link) {
        if (!saved_buf->buffer->texture) {
            continue;
        }

        struct wlr_box proj_box = {
            .x = saved_buf->x - view->saved_geometry.x - output->lx,
            .y = saved_buf->y - view->saved_geometry.y - output->ly,
            .width = saved_buf->width,
            .height = saved_buf->height,
        };

        struct wlr_box output_box = {
            .width = output->width,
            .height = output->height,
        };

        struct wlr_box intersection;
        bool intersects =
            wlr_box_intersection(&intersection, &output_box, &proj_box);
        if (!intersects) {
            continue;
        }

        struct wlr_box dst_box = proj_box;
        scale_box(&proj_box, wlr_output->scale);

        float matrix[9];
        enum wl_output_transform transform =
            wlr_output_transform_invert(saved_buf->transform);
        wlr_matrix_project_box(
            matrix, &proj_box, transform, 0, wlr_output->transform_matrix
        );

        if (!floating) {
            dst_box.width = fmin(
                dst_box.width,
                view->window->current.content_width -
                    (saved_buf->x - view->window->current.content_x) +
                    view->saved_geometry.x
            );
            dst_box.height = fmin(
                dst_box.height,
                view->window->current.content_height -
                    (saved_buf->y - view->window->current.content_y) +
                    view->saved_geometry.y
            );
        }
        scale_box(&dst_box, wlr_output->scale);

        render_texture(
            wlr_output, damage, saved_buf->buffer->texture,
            &saved_buf->source_box, &dst_box, matrix, alpha
        );
    }

    // FIXME: we should set the surface that this saved buffer originates from
    // as sampled here.
    // https://github.com/haywardwm/hayward/pull/4465#discussion_r321082059
}

/**
 * Render a view's surface and left/bottom/right borders.
 */
static void
render_view(
    struct hayward_output *output, pixman_region32_t *damage,
    struct hayward_window *window, struct border_colors *colors
) {
    struct hayward_view *view = window->view;
    if (!wl_list_empty(&view->saved_buffers)) {
        render_saved_view(view, output, damage, view->window->alpha);
    } else if (view->surface) {
        render_view_toplevels(view, output, damage, view->window->alpha);
    }

    if (window->current.border == B_NONE || window->current.border == B_CSD) {
        return;
    }

    struct wlr_box box;
    float output_scale = output->wlr_output->scale;
    float color[4];
    struct hayward_window_state *state = &window->current;

    if (state->border_left) {
        memcpy(&color, colors->child_border, sizeof(float) * 4);
        premultiply_alpha(color, window->alpha);
        box.x = floor(state->x);
        box.y = floor(state->content_y);
        box.width = state->border_thickness;
        box.height = state->content_height;
        scale_box(&box, output_scale);
        render_rect(output, damage, &box, color);
    }

    if (state->border_right) {
        memcpy(&color, colors->child_border, sizeof(float) * 4);
        premultiply_alpha(color, window->alpha);
        box.x = floor(state->content_x + state->content_width);
        box.y = floor(state->content_y);
        box.width = state->border_thickness;
        box.height = state->content_height;
        scale_box(&box, output_scale);
        render_rect(output, damage, &box, color);
    }

    if (state->border_bottom) {
        if (!window_is_current_floating(window) &&
            window_get_current_siblings(window)->length == 1 &&
            window->current.parent->current.layout == L_SPLIT) {
            memcpy(&color, colors->indicator, sizeof(float) * 4);
        } else {
            memcpy(&color, colors->child_border, sizeof(float) * 4);
        }
        premultiply_alpha(color, window->alpha);
        box.x = floor(state->x);
        box.y = floor(state->content_y + state->content_height);
        box.width = state->width;
        box.height = state->border_thickness;
        scale_box(&box, output_scale);
        render_rect(output, damage, &box, color);
    }
}

/**
 * Render a titlebar.
 *
 * Care must be taken not to render over the same pixel multiple times,
 * otherwise the colors will be incorrect when using opacity.
 *
 * The height is: 1px border, 3px padding, font height, 3px padding, 1px border
 * The left side is: 1px border, 2px padding, title
 */
static void
render_titlebar(
    struct hayward_output *output, pixman_region32_t *output_damage,
    struct hayward_window *window, int x, int y, int width,
    struct border_colors *colors, struct wlr_texture *title_texture
) {
    struct wlr_box box;
    float color[4];
    float output_scale = output->wlr_output->scale;
    double output_x = output->lx;
    double output_y = output->ly;
    int titlebar_border_thickness = config->titlebar_border_thickness;
    int titlebar_h_padding = config->titlebar_h_padding;
    int titlebar_v_padding = config->titlebar_v_padding;
    enum alignment title_align = config->title_align;

    // Single pixel bar above title
    memcpy(&color, colors->border, sizeof(float) * 4);
    premultiply_alpha(color, window->alpha);
    box.x = x;
    box.y = y;
    box.width = width;
    box.height = titlebar_border_thickness;
    scale_box(&box, output_scale);
    render_rect(output, output_damage, &box, color);

    // Single pixel bar below title
    box.x = x;
    box.y = y + window_titlebar_height() - titlebar_border_thickness;
    box.width = width;
    box.height = titlebar_border_thickness;
    scale_box(&box, output_scale);
    render_rect(output, output_damage, &box, color);

    // Single pixel left edge
    box.x = x;
    box.y = y + titlebar_border_thickness;
    box.width = titlebar_border_thickness;
    box.height = window_titlebar_height() - titlebar_border_thickness * 2;
    scale_box(&box, output_scale);
    render_rect(output, output_damage, &box, color);

    // Single pixel right edge
    box.x = x + width - titlebar_border_thickness;
    box.y = y + titlebar_border_thickness;
    box.width = titlebar_border_thickness;
    box.height = window_titlebar_height() - titlebar_border_thickness * 2;
    scale_box(&box, output_scale);
    render_rect(output, output_damage, &box, color);

    int inner_x = x - output_x + titlebar_h_padding;
    int bg_y = y + titlebar_border_thickness;
    size_t inner_width = width - titlebar_h_padding * 2;

    // output-buffer local
    int ob_inner_x = round(inner_x * output_scale);
    int ob_inner_width = scale_length(inner_width, inner_x, output_scale);
    int ob_bg_height = scale_length(
        (titlebar_v_padding - titlebar_border_thickness) * 2 +
            config->font_height,
        bg_y, output_scale
    );

    // Title text
    int ob_title_x = 0;     // output-buffer-local
    int ob_title_width = 0; // output-buffer-local
    if (title_texture) {
        struct wlr_box texture_box = {
            .width = title_texture->width,
            .height = title_texture->height,
        };

        // The effective output may be NULL when window is not on any output.
        // This can happen because we render all children of windows,
        // even those that are out of the bounds of any output.
        struct hayward_output *effective = window_get_effective_output(window);
        float title_scale =
            effective ? effective->wlr_output->scale : output_scale;
        texture_box.width = texture_box.width * output_scale / title_scale;
        texture_box.height = texture_box.height * output_scale / title_scale;
        ob_title_width = texture_box.width;

        // The title texture might be shorter than the config->font_height,
        // in which case we need to pad it above and below.
        int ob_padding_above = round(
            (titlebar_v_padding - titlebar_border_thickness) * output_scale
        );
        int ob_padding_below =
            ob_bg_height - ob_padding_above - texture_box.height;

        // Render texture
        if (texture_box.width > ob_inner_width) {
            texture_box.x = ob_inner_x;
        } else if (title_align == ALIGN_LEFT) {
            texture_box.x = ob_inner_x;
        } else if (title_align == ALIGN_CENTER) {
            // Center in the inner area.
            texture_box.x =
                ob_inner_x + ob_inner_width / 2 - texture_box.width / 2;
        } else {
            texture_box.x = ob_inner_x + ob_inner_width - texture_box.width;
        }
        ob_title_x = texture_box.x;

        texture_box.y =
            round((bg_y - output_y) * output_scale) + ob_padding_above;

        float matrix[9];
        wlr_matrix_project_box(
            matrix, &texture_box, WL_OUTPUT_TRANSFORM_NORMAL, 0.0,
            output->wlr_output->transform_matrix
        );

        if (ob_inner_width < texture_box.width) {
            texture_box.width = ob_inner_width;
        }

        render_texture(
            output->wlr_output, output_damage, title_texture, NULL,
            &texture_box, matrix, window->alpha
        );

        // Padding above
        memcpy(&color, colors->background, sizeof(float) * 4);
        premultiply_alpha(color, window->alpha);
        box.x = texture_box.x + round(output_x * output_scale);
        box.y = round((y + titlebar_border_thickness) * output_scale);
        box.width = texture_box.width;
        box.height = ob_padding_above;
        render_rect(output, output_damage, &box, color);

        // Padding below
        box.y += ob_padding_above + texture_box.height;
        box.height = ob_padding_below;
        render_rect(output, output_damage, &box, color);
    }

    // Determine the left + right extends of the textures (output-buffer local)
    int ob_left_x, ob_left_width, ob_right_x, ob_right_width;
    if (ob_title_width == 0) {
        ob_left_x = ob_inner_x;
        ob_left_width = 0;
        ob_right_x = ob_inner_x;
        ob_right_width = 0;
    } else {
        ob_left_x = 0;
        ob_left_width = 0;
        ob_right_x = ob_title_x;
        ob_right_width = ob_title_width;
    }
    if (ob_left_x < ob_inner_x) {
        ob_left_x = ob_inner_x;
    } else if (ob_left_x + ob_left_width > ob_right_x + ob_right_width) {
        ob_right_x = ob_left_x;
        ob_right_width = ob_left_width;
    }

    // Filler
    box.width = ob_right_x - ob_left_x - ob_left_width;
    if (box.width > 0) {
        box.x = ob_left_x + ob_left_width + round(output_x * output_scale);
        box.y = round(bg_y * output_scale);
        box.height = ob_bg_height;
        render_rect(output, output_damage, &box, color);
    }

    // Padding on left side
    box.x = x + titlebar_border_thickness;
    box.y = y + titlebar_border_thickness;
    box.width = titlebar_h_padding - titlebar_border_thickness;
    box.height = (titlebar_v_padding - titlebar_border_thickness) * 2 +
        config->font_height;
    scale_box(&box, output_scale);
    int left_x = ob_left_x + round(output_x * output_scale);
    if (box.x + box.width < left_x) {
        box.width += left_x - box.x - box.width;
    }
    render_rect(output, output_damage, &box, color);

    // Padding on right side
    box.x = x + width - titlebar_h_padding;
    box.y = y + titlebar_border_thickness;
    box.width = titlebar_h_padding - titlebar_border_thickness;
    box.height = (titlebar_v_padding - titlebar_border_thickness) * 2 +
        config->font_height;
    scale_box(&box, output_scale);
    int right_rx = ob_right_x + ob_right_width + round(output_x * output_scale);
    if (right_rx < box.x) {
        box.width += box.x - right_rx;
        box.x = right_rx;
    }
    render_rect(output, output_damage, &box, color);
}

/**
 * Render the top border line for a view using "border pixel".
 */
static void
render_top_border(
    struct hayward_output *output, pixman_region32_t *output_damage,
    struct hayward_window *container, struct border_colors *colors
) {
    struct hayward_window_state *state = &container->current;
    if (!state->border_top) {
        return;
    }
    struct wlr_box box;
    float color[4];
    float output_scale = output->wlr_output->scale;

    // Child border - top edge
    memcpy(&color, colors->child_border, sizeof(float) * 4);
    premultiply_alpha(color, container->alpha);
    box.x = floor(state->x);
    box.y = floor(state->y);
    box.width = state->width;
    box.height = state->border_thickness;
    scale_box(&box, output_scale);
    render_rect(output, output_damage, &box, color);
}

/**
 * Render a container's children using an L_SPLIT layout.
 *
 * Wrap child views in borders and leave child containers borderless because
 * they'll apply their own borders to their children.
 */
static void
render_column_split(
    struct hayward_output *output, pixman_region32_t *damage,
    struct hayward_column *column
) {
    struct hayward_window *current = column->current.active_child;

    for (int i = 0; i < column->current.children->length; ++i) {
        struct hayward_window *child = column->current.children->items[i];

        struct hayward_view *view = child->view;
        struct border_colors *colors;
        struct wlr_texture *title_texture;
        struct hayward_window_state *state = &child->current;

        if (view_is_urgent(view)) {
            colors = &config->border_colors.urgent;
            title_texture = child->title_urgent;
        } else if (state->focused) {
            colors = &config->border_colors.focused;
            title_texture = child->title_focused;
        } else if (child == current) {
            colors = &config->border_colors.focused_inactive;
            title_texture = child->title_focused_inactive;
        } else {
            colors = &config->border_colors.unfocused;
            title_texture = child->title_unfocused;
        }

        if (state->border == B_NORMAL) {
            render_titlebar(
                output, damage, child, floor(state->x), floor(state->y),
                state->width, colors, title_texture
            );
        } else if (state->border == B_PIXEL) {
            render_top_border(output, damage, child, colors);
        }
        render_view(output, damage, child, colors);
    }
}

/**
 * Render a container's children using the L_STACKED layout.
 */
static void
render_column_stacked(
    struct hayward_output *output, pixman_region32_t *damage,
    struct hayward_column *column
) {
    if (!column->current.children->length) {
        return;
    }
    struct hayward_window *current = column->current.active_child;
    struct border_colors *current_colors = &config->border_colors.unfocused;
    size_t titlebar_height = window_titlebar_height();

    int y_offset = column->current.y;

    // Render titles
    for (int i = 0; i < column->current.children->length; ++i) {
        struct hayward_window *child = column->current.children->items[i];
        struct hayward_view *view = child->view;
        struct hayward_window_state *cstate = &child->current;
        struct border_colors *colors;
        struct wlr_texture *title_texture;

        if (view_is_urgent(view)) {
            colors = &config->border_colors.urgent;
            title_texture = child->title_urgent;
        } else if (cstate->focused) {
            colors = &config->border_colors.focused;
            title_texture = child->title_focused;
        } else if (child == current) {
            colors = &config->border_colors.focused_inactive;
            title_texture = child->title_focused_inactive;
        } else {
            colors = &config->border_colors.unfocused;
            title_texture = child->title_unfocused;
        }

        render_titlebar(
            output, damage, child, column->current.x, y_offset,
            column->current.width, colors, title_texture
        );

        y_offset += titlebar_height;

        if (child == current) {
            current_colors = colors;
            y_offset += column->current.height -
                titlebar_height * column->current.children->length;
        }
    }

    // Render surface and left/right/bottom borders
    render_view(output, damage, current, current_colors);
}

static void
render_column(
    struct hayward_output *output, pixman_region32_t *damage,
    struct hayward_column *column
) {
    switch (column->current.layout) {
    case L_SPLIT:
        render_column_split(output, damage, column);
        break;
    case L_STACKED:
        render_column_stacked(output, damage, column);
        break;
    }
}

static void
render_workspace(
    struct hayward_output *output, pixman_region32_t *damage,
    struct hayward_workspace *workspace, bool focused
) {
    for (int i = 0; i < workspace->current.tiling->length; ++i) {
        struct hayward_column *column = workspace->current.tiling->items[i];
        if (column->current.output != output) {
            continue;
        }
        render_column(output, damage, column);
    }
}

static void
render_floating_window(
    struct hayward_output *soutput, pixman_region32_t *damage,
    struct hayward_window *window
) {
    struct hayward_view *view = window->view;
    struct border_colors *colors;
    struct wlr_texture *title_texture;

    if (view_is_urgent(view)) {
        colors = &config->border_colors.urgent;
        title_texture = window->title_urgent;
    } else if (window->current.focused) {
        colors = &config->border_colors.focused;
        title_texture = window->title_focused;
    } else {
        colors = &config->border_colors.unfocused;
        title_texture = window->title_unfocused;
    }

    if (window->current.border == B_NORMAL) {
        render_titlebar(
            soutput, damage, window, floor(window->current.x),
            floor(window->current.y), window->current.width, colors,
            title_texture
        );
    } else if (window->current.border == B_PIXEL) {
        render_top_border(soutput, damage, window, colors);
    }
    render_view(soutput, damage, window, colors);
}

static void
render_floating(struct hayward_output *output, pixman_region32_t *damage) {
    struct hayward_workspace *workspace = root_get_active_workspace();
    hayward_assert(workspace != NULL, "Expected active workspace");

    for (int i = 0; i < workspace->current.floating->length; ++i) {
        struct hayward_window *floater = workspace->current.floating->items[i];
        if (floater->current.fullscreen) {
            continue;
        }
        render_floating_window(output, damage, floater);
    }
}

static void
render_seatops(struct hayward_output *output, pixman_region32_t *damage) {
    struct hayward_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) {
        seatop_render(seat, output, damage);
    }
}

void
output_render(
    struct hayward_output *output, struct timespec *when,
    pixman_region32_t *damage
) {
    struct wlr_output *wlr_output = output->wlr_output;
    struct wlr_renderer *renderer = output->server->renderer;

    struct hayward_workspace *workspace = root_get_current_active_workspace();
    if (workspace == NULL) {
        return;
    }

    struct hayward_window *fullscreen_window =
        output->current.fullscreen_window;

    wlr_renderer_begin(renderer, wlr_output->width, wlr_output->height);

    if (debug.damage == DAMAGE_RERENDER) {
        int width, height;
        wlr_output_transformed_resolution(wlr_output, &width, &height);
        pixman_region32_union_rect(damage, damage, 0, 0, width, height);
    }

    if (!pixman_region32_not_empty(damage)) {
        // Output isn't damaged but needs buffer swap
        goto renderer_end;
    }

    if (debug.damage == DAMAGE_HIGHLIGHT) {
        wlr_renderer_clear(renderer, (float[]){1, 1, 0, 1});
    }

    if (server.session_lock.locked) {
        float clear_color[] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (server.session_lock.lock == NULL) {
            // abandoned lock -> red BG
            clear_color[0] = 1.f;
        }
        int nrects;
        pixman_box32_t *rects = pixman_region32_rectangles(damage, &nrects);
        for (int i = 0; i < nrects; ++i) {
            scissor_output(wlr_output, &rects[i]);
            wlr_renderer_clear(renderer, clear_color);
        }

        if (server.session_lock.lock != NULL) {
            struct render_data data = {
                .damage = damage,
                .alpha = 1.0f,
            };

            struct wlr_session_lock_surface_v1 *lock_surface;
            wl_list_for_each(
                lock_surface, &server.session_lock.lock->surfaces, link
            ) {
                if (lock_surface->output != wlr_output) {
                    continue;
                }
                if (!lock_surface->mapped) {
                    continue;
                }

                output_surface_for_each_surface(
                    output, lock_surface->surface, 0.0, 0.0,
                    render_surface_iterator, &data
                );
            }
        }
        goto renderer_end;
    }

    if (output_has_opaque_overlay_layer_surface(output)) {
        goto render_overlay;
    }

    if (fullscreen_window) {
        float clear_color[] = {0.0f, 0.0f, 0.0f, 1.0f};

        int nrects;
        pixman_box32_t *rects = pixman_region32_rectangles(damage, &nrects);
        for (int i = 0; i < nrects; ++i) {
            scissor_output(wlr_output, &rects[i]);
            wlr_renderer_clear(renderer, clear_color);
        }

        if (fullscreen_window->view) {
            if (!wl_list_empty(&fullscreen_window->view->saved_buffers)) {
                render_saved_view(
                    fullscreen_window->view, output, damage, 1.0f
                );
            } else if (fullscreen_window->view->surface) {
                render_view_toplevels(
                    fullscreen_window->view, output, damage, 1.0f
                );
            }
        } else {
            render_view(
                output, damage, fullscreen_window,
                &config->border_colors.focused
            );
        }

        for (int i = 0; i < workspace->current.floating->length; ++i) {
            struct hayward_window *floater =
                workspace->current.floating->items[i];
            if (window_is_transient_for(floater, fullscreen_window)) {
                render_floating_window(output, damage, floater);
            }
        }
#if HAVE_XWAYLAND
        render_unmanaged(output, damage, &root->xwayland_unmanaged);
#endif
    } else {
        float clear_color[] = {0.25f, 0.25f, 0.25f, 1.0f};

        int nrects;
        pixman_box32_t *rects = pixman_region32_rectangles(damage, &nrects);
        for (int i = 0; i < nrects; ++i) {
            scissor_output(wlr_output, &rects[i]);
            wlr_renderer_clear(renderer, clear_color);
        }

        render_layer_toplevel(
            output, damage,
            &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]
        );
        render_layer_toplevel(
            output, damage, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]
        );

        render_workspace(output, damage, workspace, workspace->current.focused);
        render_floating(output, damage);
#if HAVE_XWAYLAND
        render_unmanaged(output, damage, &root->xwayland_unmanaged);
#endif
        render_layer_toplevel(
            output, damage, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]
        );

        render_layer_popups(
            output, damage,
            &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]
        );
        render_layer_popups(
            output, damage, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]
        );
        render_layer_popups(
            output, damage, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]
        );
    }

    render_seatops(output, damage);

    struct hayward_window *focus = root_get_focused_window();
    if (focus && focus->view) {
        render_view_popups(focus->view, output, damage, focus->alpha);
    }

render_overlay:
    render_layer_toplevel(
        output, damage, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]
    );
    render_layer_popups(
        output, damage, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]
    );
    render_drag_icons(output, damage, &root->drag_icons);

renderer_end:
    wlr_renderer_scissor(renderer, NULL);
    wlr_output_render_software_cursors(wlr_output, damage);
    wlr_renderer_end(renderer);

    int width, height;
    wlr_output_transformed_resolution(wlr_output, &width, &height);

    pixman_region32_t frame_damage;
    pixman_region32_init(&frame_damage);

    enum wl_output_transform transform =
        wlr_output_transform_invert(wlr_output->transform);
    wlr_region_transform(
        &frame_damage, &output->damage->current, transform, width, height
    );

    if (debug.damage != DAMAGE_DEFAULT) {
        pixman_region32_union_rect(
            &frame_damage, &frame_damage, 0, 0, wlr_output->width,
            wlr_output->height
        );
    }

    wlr_output_set_damage(wlr_output, &frame_damage);
    pixman_region32_fini(&frame_damage);

    if (!wlr_output_commit(wlr_output)) {
        return;
    }
    output->last_frame = *when;
}
