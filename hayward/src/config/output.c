#define _POSIX_C_SOURCE 200809L
#include "hayward/output.h"

#include <assert.h>
#include <drm_fourcc.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wlr/backend/drm.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>

#include "hayward-common/log.h"
#include "hayward-common/util.h"

#include "hayward/config.h"
#include "hayward/input/cursor.h"
#include "hayward/tree/root.h"

int output_name_cmp(const void *item, const void *data) {
	const struct output_config *output = item;
	const char *name = data;

	return strcmp(output->name, name);
}

void output_get_identifier(
	char *identifier, size_t len, struct hayward_output *output
) {
	struct wlr_output *wlr_output = output->wlr_output;
	snprintf(
		identifier, len, "%s %s %s", wlr_output->make, wlr_output->model,
		wlr_output->serial
	);
}

const char *
hayward_output_scale_filter_to_string(enum scale_filter_mode scale_filter) {
	switch (scale_filter) {
	case SCALE_FILTER_DEFAULT:
		return "smart";
	case SCALE_FILTER_LINEAR:
		return "linear";
	case SCALE_FILTER_NEAREST:
		return "nearest";
	case SCALE_FILTER_SMART:
		return "smart";
	}
	hayward_assert(false, "Unknown value for scale_filter.");
	return NULL;
}

struct output_config *new_output_config(const char *name) {
	struct output_config *oc = calloc(1, sizeof(struct output_config));
	if (oc == NULL) {
		return NULL;
	}
	oc->name = strdup(name);
	if (oc->name == NULL) {
		free(oc);
		return NULL;
	}
	oc->enabled = -1;
	oc->width = oc->height = -1;
	oc->refresh_rate = -1;
	oc->custom_mode = -1;
	oc->drm_mode.type = -1;
	oc->x = oc->y = -1;
	oc->scale = -1;
	oc->scale_filter = SCALE_FILTER_DEFAULT;
	oc->transform = -1;
	oc->subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;
	oc->max_render_time = -1;
	oc->adaptive_sync = -1;
	oc->render_bit_depth = RENDER_BIT_DEPTH_DEFAULT;
	return oc;
}

void merge_output_config(struct output_config *dst, struct output_config *src) {
	if (src->enabled != -1) {
		dst->enabled = src->enabled;
	}
	if (src->width != -1) {
		dst->width = src->width;
	}
	if (src->height != -1) {
		dst->height = src->height;
	}
	if (src->x != -1) {
		dst->x = src->x;
	}
	if (src->y != -1) {
		dst->y = src->y;
	}
	if (src->scale != -1) {
		dst->scale = src->scale;
	}
	if (src->scale_filter != SCALE_FILTER_DEFAULT) {
		dst->scale_filter = src->scale_filter;
	}
	if (src->subpixel != WL_OUTPUT_SUBPIXEL_UNKNOWN) {
		dst->subpixel = src->subpixel;
	}
	if (src->refresh_rate != -1) {
		dst->refresh_rate = src->refresh_rate;
	}
	if (src->custom_mode != -1) {
		dst->custom_mode = src->custom_mode;
	}
	if (src->drm_mode.type != (uint32_t)-1) {
		memcpy(&dst->drm_mode, &src->drm_mode, sizeof(src->drm_mode));
	}
	if (src->transform != -1) {
		dst->transform = src->transform;
	}
	if (src->max_render_time != -1) {
		dst->max_render_time = src->max_render_time;
	}
	if (src->adaptive_sync != -1) {
		dst->adaptive_sync = src->adaptive_sync;
	}
	if (src->render_bit_depth != RENDER_BIT_DEPTH_DEFAULT) {
		dst->render_bit_depth = src->render_bit_depth;
	}
	if (src->background) {
		free(dst->background);
		dst->background = strdup(src->background);
	}
	if (src->background_option) {
		free(dst->background_option);
		dst->background_option = strdup(src->background_option);
	}
	if (src->background_fallback) {
		free(dst->background_fallback);
		dst->background_fallback = strdup(src->background_fallback);
	}
	if (src->dpms_state != 0) {
		dst->dpms_state = src->dpms_state;
	}
}

static void merge_wildcard_on_all(struct output_config *wildcard) {
	for (int i = 0; i < config->output_configs->length; i++) {
		struct output_config *oc = config->output_configs->items[i];
		if (strcmp(wildcard->name, oc->name) != 0) {
			hayward_log(
				HAYWARD_DEBUG, "Merging output * config on %s", oc->name
			);
			merge_output_config(oc, wildcard);
		}
	}
}

static void merge_id_on_name(struct output_config *oc) {
	char *id_on_name = NULL;
	char id[128];
	char *name = NULL;
	struct hayward_output *output;
	wl_list_for_each(output, &root->all_outputs, link) {
		name = output->wlr_output->name;
		output_get_identifier(id, sizeof(id), output);
		if (strcmp(name, oc->name) == 0 || strcmp(id, oc->name) == 0) {
			size_t length = snprintf(NULL, 0, "%s on %s", id, name) + 1;
			id_on_name = malloc(length);
			if (!id_on_name) {
				hayward_log(
					HAYWARD_ERROR, "Failed to allocate id on name string"
				);
				return;
			}
			snprintf(id_on_name, length, "%s on %s", id, name);
			break;
		}
	}

	if (!id_on_name) {
		return;
	}

	int i = list_seq_find(config->output_configs, output_name_cmp, id_on_name);
	if (i >= 0) {
		hayward_log(
			HAYWARD_DEBUG, "Merging on top of existing id on name config"
		);
		merge_output_config(config->output_configs->items[i], oc);
	} else {
		// If both a name and identifier config, exist generate an id on name
		int ni = list_seq_find(config->output_configs, output_name_cmp, name);
		int ii = list_seq_find(config->output_configs, output_name_cmp, id);
		if ((ni >= 0 && ii >= 0) || (ni >= 0 && strcmp(oc->name, id) == 0) ||
			(ii >= 0 && strcmp(oc->name, name) == 0)) {
			struct output_config *ion_oc = new_output_config(id_on_name);
			if (ni >= 0) {
				merge_output_config(ion_oc, config->output_configs->items[ni]);
			}
			if (ii >= 0) {
				merge_output_config(ion_oc, config->output_configs->items[ii]);
			}
			merge_output_config(ion_oc, oc);
			list_add(config->output_configs, ion_oc);
			hayward_log(
				HAYWARD_DEBUG,
				"Generated id on name output config \"%s\""
				" (enabled: %d) (%dx%d@%fHz position %d,%d scale %f "
				"transform %d) (bg %s %s) (dpms %d) (max render time: %d)",
				ion_oc->name, ion_oc->enabled, ion_oc->width, ion_oc->height,
				ion_oc->refresh_rate, ion_oc->x, ion_oc->y, ion_oc->scale,
				ion_oc->transform, ion_oc->background,
				ion_oc->background_option, ion_oc->dpms_state,
				ion_oc->max_render_time
			);
		}
	}
	free(id_on_name);
}

struct output_config *store_output_config(struct output_config *oc) {
	bool wildcard = strcmp(oc->name, "*") == 0;
	if (wildcard) {
		merge_wildcard_on_all(oc);
	} else {
		merge_id_on_name(oc);
	}

	int i = list_seq_find(config->output_configs, output_name_cmp, oc->name);
	if (i >= 0) {
		hayward_log(HAYWARD_DEBUG, "Merging on top of existing output config");
		struct output_config *current = config->output_configs->items[i];
		merge_output_config(current, oc);
		free_output_config(oc);
		oc = current;
	} else if (!wildcard) {
		hayward_log(HAYWARD_DEBUG, "Adding non-wildcard output config");
		i = list_seq_find(config->output_configs, output_name_cmp, "*");
		if (i >= 0) {
			hayward_log(HAYWARD_DEBUG, "Merging on top of output * config");
			struct output_config *current = new_output_config(oc->name);
			merge_output_config(current, config->output_configs->items[i]);
			merge_output_config(current, oc);
			free_output_config(oc);
			oc = current;
		}
		list_add(config->output_configs, oc);
	} else {
		// New wildcard config. Just add it
		hayward_log(HAYWARD_DEBUG, "Adding output * config");
		list_add(config->output_configs, oc);
	}

	hayward_log(
		HAYWARD_DEBUG,
		"Config stored for output %s (enabled: %d) (%dx%d@%fHz "
		"position %d,%d scale %f subpixel %s transform %d) (bg %s %s) (dpms "
		"%d) "
		"(max render time: %d)",
		oc->name, oc->enabled, oc->width, oc->height, oc->refresh_rate, oc->x,
		oc->y, oc->scale, hayward_wl_output_subpixel_to_string(oc->subpixel),
		oc->transform, oc->background, oc->background_option, oc->dpms_state,
		oc->max_render_time
	);

	return oc;
}

static void set_mode(
	struct wlr_output *output, int width, int height, float refresh_rate,
	bool custom
) {
	// Not all floating point integers can be represented exactly
	// as (int)(1000 * mHz / 1000.f)
	// round() the result to avoid any error
	int mhz = (int)round(refresh_rate * 1000);

	if (wl_list_empty(&output->modes) || custom) {
		hayward_log(HAYWARD_DEBUG, "Assigning custom mode to %s", output->name);
		wlr_output_set_custom_mode(
			output, width, height, refresh_rate > 0 ? mhz : 0
		);
		return;
	}

	struct wlr_output_mode *mode, *best = NULL;
	wl_list_for_each(mode, &output->modes, link) {
		if (mode->width == width && mode->height == height) {
			if (mode->refresh == mhz) {
				best = mode;
				break;
			}
			if (best == NULL || mode->refresh > best->refresh) {
				best = mode;
			}
		}
	}
	if (!best) {
		hayward_log(
			HAYWARD_ERROR, "Configured mode for %s not available", output->name
		);
		hayward_log(HAYWARD_INFO, "Picking preferred mode instead");
		best = wlr_output_preferred_mode(output);
	} else {
		hayward_log(
			HAYWARD_DEBUG, "Assigning configured mode to %s", output->name
		);
	}
	wlr_output_set_mode(output, best);
}

static void set_modeline(struct wlr_output *output, drmModeModeInfo *drm_mode) {
	if (!wlr_output_is_drm(output)) {
		hayward_log(HAYWARD_ERROR, "Modeline can only be set to DRM output");
		return;
	}
	hayward_log(HAYWARD_DEBUG, "Assigning custom modeline to %s", output->name);
	struct wlr_output_mode *mode = wlr_drm_connector_add_mode(output, drm_mode);
	if (mode) {
		wlr_output_set_mode(output, mode);
	}
}

/* Some manufacturers hardcode the aspect-ratio of the output in the physical
 * size field. */
static bool phys_size_is_aspect_ratio(struct wlr_output *output) {
	return (output->phys_width == 1600 && output->phys_height == 900) ||
		(output->phys_width == 1600 && output->phys_height == 1000) ||
		(output->phys_width == 160 && output->phys_height == 90) ||
		(output->phys_width == 160 && output->phys_height == 100) ||
		(output->phys_width == 16 && output->phys_height == 9) ||
		(output->phys_width == 16 && output->phys_height == 10);
}

// The minimum DPI at which we turn on a scale of 2
#define HIDPI_DPI_LIMIT (2 * 96)
// The minimum screen height at which we turn on a scale of 2
#define HIDPI_MIN_HEIGHT 1200
// 1 inch = 25.4 mm
#define MM_PER_INCH 25.4

static int compute_default_scale(struct wlr_output *output) {
	struct wlr_box box = {.width = output->width, .height = output->height};
	if (output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		switch (output->pending.mode_type) {
		case WLR_OUTPUT_STATE_MODE_FIXED:
			box.width = output->pending.mode->width;
			box.height = output->pending.mode->height;
			break;
		case WLR_OUTPUT_STATE_MODE_CUSTOM:
			box.width = output->pending.custom_mode.width;
			box.height = output->pending.custom_mode.height;
			break;
		}
	}
	enum wl_output_transform transform = output->transform;
	if (output->pending.committed & WLR_OUTPUT_STATE_TRANSFORM) {
		transform = output->pending.transform;
	}
	wlr_box_transform(&box, &box, transform, box.width, box.height);

	int width = box.width;
	int height = box.height;

	if (height < HIDPI_MIN_HEIGHT) {
		return 1;
	}

	if (output->phys_width == 0 || output->phys_height == 0) {
		return 1;
	}

	if (phys_size_is_aspect_ratio(output)) {
		return 1;
	}

	double dpi_x = (double)width / (output->phys_width / MM_PER_INCH);
	double dpi_y = (double)height / (output->phys_height / MM_PER_INCH);
	hayward_log(HAYWARD_DEBUG, "Output DPI: %fx%f", dpi_x, dpi_y);
	if (dpi_x <= HIDPI_DPI_LIMIT || dpi_y <= HIDPI_DPI_LIMIT) {
		return 1;
	}

	return 2;
}

/* Lists of formats to try, in order, when a specific render bit depth has
 * been asked for. The second to last format in each list should always
 * be XRGB8888, as a reliable backup in case the others are not available;
 * the last should be DRM_FORMAT_INVALID, to indicate the end of the list. */
static const uint32_t *bit_depth_preferences[] = {
	[RENDER_BIT_DEPTH_8] =
		(const uint32_t[]){
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_INVALID,
		},
	[RENDER_BIT_DEPTH_10] =
		(const uint32_t[]){
			DRM_FORMAT_XRGB2101010,
			DRM_FORMAT_XBGR2101010,
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_INVALID,
		},
};

static void
queue_output_config(struct output_config *oc, struct hayward_output *output) {
	if (output == root->fallback_output) {
		return;
	}

	struct wlr_output *wlr_output = output->wlr_output;

	if (oc && (!oc->enabled || oc->dpms_state == DPMS_OFF)) {
		hayward_log(HAYWARD_DEBUG, "Turning off output %s", wlr_output->name);
		wlr_output_enable(wlr_output, false);
		return;
	}

	hayward_log(HAYWARD_DEBUG, "Turning on output %s", wlr_output->name);
	wlr_output_enable(wlr_output, true);

	if (oc && oc->drm_mode.type != 0 && oc->drm_mode.type != (uint32_t)-1) {
		hayward_log(HAYWARD_DEBUG, "Set %s modeline", wlr_output->name);
		set_modeline(wlr_output, &oc->drm_mode);
	} else if (oc && oc->width > 0 && oc->height > 0) {
		hayward_log(
			HAYWARD_DEBUG, "Set %s mode to %dx%d (%f Hz)", wlr_output->name,
			oc->width, oc->height, oc->refresh_rate
		);
		set_mode(
			wlr_output, oc->width, oc->height, oc->refresh_rate,
			oc->custom_mode == 1
		);
	} else if (!wl_list_empty(&wlr_output->modes)) {
		hayward_log(HAYWARD_DEBUG, "Set preferred mode");
		struct wlr_output_mode *preferred_mode =
			wlr_output_preferred_mode(wlr_output);
		wlr_output_set_mode(wlr_output, preferred_mode);

		if (!wlr_output_test(wlr_output)) {
			hayward_log(
				HAYWARD_DEBUG,
				"Preferred mode rejected, "
				"falling back to another mode"
			);
			struct wlr_output_mode *mode;
			wl_list_for_each(mode, &wlr_output->modes, link) {
				if (mode == preferred_mode) {
					continue;
				}

				wlr_output_set_mode(wlr_output, mode);
				if (wlr_output_test(wlr_output)) {
					break;
				}
			}
		}
	}

	if (oc &&
		(oc->subpixel != WL_OUTPUT_SUBPIXEL_UNKNOWN || config->reloading)) {
		hayward_log(
			HAYWARD_DEBUG, "Set %s subpixel to %s", oc->name,
			hayward_wl_output_subpixel_to_string(oc->subpixel)
		);
		wlr_output_set_subpixel(wlr_output, oc->subpixel);
	}

	enum wl_output_transform tr = WL_OUTPUT_TRANSFORM_NORMAL;
	if (oc && oc->transform >= 0) {
		tr = oc->transform;
	} else if (wlr_output_is_drm(wlr_output)) {
		tr = wlr_drm_connector_get_panel_orientation(wlr_output);
		hayward_log(HAYWARD_DEBUG, "Auto-detected output transform: %d", tr);
	}
	if (wlr_output->transform != tr) {
		hayward_log(HAYWARD_DEBUG, "Set %s transform to %d", oc->name, tr);
		wlr_output_set_transform(wlr_output, tr);
	}

	// Apply the scale last before the commit, because the scale auto-detection
	// reads the pending output size
	float scale;
	if (oc && oc->scale > 0) {
		scale = oc->scale;
	} else {
		scale = compute_default_scale(wlr_output);
		hayward_log(HAYWARD_DEBUG, "Auto-detected output scale: %f", scale);
	}
	if (scale != wlr_output->scale) {
		hayward_log(
			HAYWARD_DEBUG, "Set %s scale to %f", wlr_output->name, scale
		);
		wlr_output_set_scale(wlr_output, scale);
	}

	if (oc && oc->adaptive_sync != -1) {
		hayward_log(
			HAYWARD_DEBUG, "Set %s adaptive sync to %d", wlr_output->name,
			oc->adaptive_sync
		);
		wlr_output_enable_adaptive_sync(wlr_output, oc->adaptive_sync == 1);
	}

	if (oc && oc->render_bit_depth != RENDER_BIT_DEPTH_DEFAULT) {
		const uint32_t *fmts = bit_depth_preferences[oc->render_bit_depth];
		assert(fmts);

		for (size_t i = 0; fmts[i] != DRM_FORMAT_INVALID; i++) {
			wlr_output_set_render_format(wlr_output, fmts[i]);
			if (wlr_output_test(wlr_output)) {
				break;
			}

			hayward_log(
				HAYWARD_DEBUG,
				"Preferred output format 0x%08x "
				"failed to work, falling back to next in "
				"list, 0x%08x",
				fmts[i], fmts[i + 1]
			);
		}
	}
}

bool apply_output_config(
	struct output_config *oc, struct hayward_output *output
) {
	if (output == root->fallback_output) {
		return false;
	}

	struct wlr_output *wlr_output = output->wlr_output;

	// Flag to prevent the output mode event handler from calling us
	output->enabling = (!oc || oc->enabled);

	queue_output_config(oc, output);

	if (!oc || oc->dpms_state != DPMS_OFF) {
		output->current_mode = wlr_output->pending.mode;
	}

	hayward_log(HAYWARD_DEBUG, "Committing output %s", wlr_output->name);
	if (!wlr_output_commit(wlr_output)) {
		// Failed to commit output changes, maybe the output is missing a CRTC.
		// Leave the output disabled for now and try again when the output gets
		// the mode we asked for.
		hayward_log(
			HAYWARD_ERROR, "Failed to commit output %s", wlr_output->name
		);
		output->enabling = false;
		return false;
	}

	output->enabling = false;

	if (oc && !oc->enabled) {
		hayward_log(HAYWARD_DEBUG, "Disabling output %s", oc->name);
		if (output->enabled) {
			output_disable(output);
			wlr_output_layout_remove(root->output_layout, wlr_output);
		}
		return true;
	}

	if (config->reloading) {
		output_damage_whole(output);
	}

	if (oc) {
		enum scale_filter_mode scale_filter_old = output->scale_filter;
		switch (oc->scale_filter) {
		case SCALE_FILTER_DEFAULT:
		case SCALE_FILTER_SMART:
			output->scale_filter = ceilf(wlr_output->scale) == wlr_output->scale
				? SCALE_FILTER_NEAREST
				: SCALE_FILTER_LINEAR;
			break;
		case SCALE_FILTER_LINEAR:
		case SCALE_FILTER_NEAREST:
			output->scale_filter = oc->scale_filter;
			break;
		}
		if (scale_filter_old != output->scale_filter) {
			hayward_log(
				HAYWARD_DEBUG, "Set %s scale_filter to %s", oc->name,
				hayward_output_scale_filter_to_string(output->scale_filter)
			);
		}
	}

	// Find position for it
	if (oc && (oc->x != -1 || oc->y != -1)) {
		hayward_log(
			HAYWARD_DEBUG, "Set %s position to %d, %d", oc->name, oc->x, oc->y
		);
		wlr_output_layout_add(root->output_layout, wlr_output, oc->x, oc->y);
	} else {
		wlr_output_layout_add_auto(root->output_layout, wlr_output);
	}

	// Update output->{lx, ly, width, height}
	struct wlr_box output_box;
	wlr_output_layout_get_box(root->output_layout, wlr_output, &output_box);
	output->lx = output_box.x;
	output->ly = output_box.y;
	output->width = output_box.width;
	output->height = output_box.height;

	if (!output->enabled) {
		output_enable(output);
	}

	if (oc && oc->max_render_time >= 0) {
		hayward_log(
			HAYWARD_DEBUG, "Set %s max render time to %d", oc->name,
			oc->max_render_time
		);
		output->max_render_time = oc->max_render_time;
	}

	// Reconfigure all devices, since input config may have been applied before
	// this output came online, and some config items (like map_to_output) are
	// dependent on an output being present.
	input_manager_configure_all_inputs();
	// Reconfigure the cursor images, since the scale may have changed.
	input_manager_configure_xcursor();
	return true;
}

bool test_output_config(
	struct output_config *oc, struct hayward_output *output
) {
	if (output == root->fallback_output) {
		return false;
	}

	queue_output_config(oc, output);
	bool ok = wlr_output_test(output->wlr_output);
	wlr_output_rollback(output->wlr_output);
	return ok;
}

static void
default_output_config(struct output_config *oc, struct wlr_output *wlr_output) {
	oc->enabled = 1;
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		oc->width = mode->width;
		oc->height = mode->height;
		oc->refresh_rate = mode->refresh / 1000.f;
	}
	oc->x = oc->y = -1;
	oc->scale = 0; // auto
	oc->scale_filter = SCALE_FILTER_DEFAULT;
	struct hayward_output *output = wlr_output->data;
	oc->subpixel = output->detected_subpixel;
	oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
	oc->dpms_state = DPMS_ON;
	oc->max_render_time = 0;
}

static struct output_config *
get_output_config(char *identifier, struct hayward_output *hayward_output) {
	const char *name = hayward_output->wlr_output->name;

	struct output_config *oc_id_on_name = NULL;
	struct output_config *oc_name = NULL;
	struct output_config *oc_id = NULL;

	size_t length = snprintf(NULL, 0, "%s on %s", identifier, name) + 1;
	char *id_on_name = malloc(length);
	snprintf(id_on_name, length, "%s on %s", identifier, name);
	int i = list_seq_find(config->output_configs, output_name_cmp, id_on_name);
	if (i >= 0) {
		oc_id_on_name = config->output_configs->items[i];
	} else {
		i = list_seq_find(config->output_configs, output_name_cmp, name);
		if (i >= 0) {
			oc_name = config->output_configs->items[i];
		}

		i = list_seq_find(config->output_configs, output_name_cmp, identifier);
		if (i >= 0) {
			oc_id = config->output_configs->items[i];
		}
	}

	struct output_config *result = new_output_config("temp");
	if (config->reloading) {
		default_output_config(result, hayward_output->wlr_output);
	}
	if (oc_id_on_name) {
		// Already have an identifier on name config, use that
		free(result->name);
		result->name = strdup(id_on_name);
		merge_output_config(result, oc_id_on_name);
	} else if (oc_name && oc_id) {
		// Generate a config named `<identifier> on <name>` which contains a
		// merged copy of the identifier on name. This will make sure that both
		// identifier and name configs are respected, with identifier getting
		// priority
		struct output_config *temp = new_output_config(id_on_name);
		merge_output_config(temp, oc_name);
		merge_output_config(temp, oc_id);
		list_add(config->output_configs, temp);

		free(result->name);
		result->name = strdup(id_on_name);
		merge_output_config(result, temp);

		hayward_log(
			HAYWARD_DEBUG,
			"Generated output config \"%s\" (enabled: %d)"
			" (%dx%d@%fHz position %d,%d scale %f transform %d) (bg %s %s)"
			" (dpms %d) (max render time: %d)",
			result->name, result->enabled, result->width, result->height,
			result->refresh_rate, result->x, result->y, result->scale,
			result->transform, result->background, result->background_option,
			result->dpms_state, result->max_render_time
		);
	} else if (oc_name) {
		// No identifier config, just return a copy of the name config
		free(result->name);
		result->name = strdup(name);
		merge_output_config(result, oc_name);
	} else if (oc_id) {
		// No name config, just return a copy of the identifier config
		free(result->name);
		result->name = strdup(identifier);
		merge_output_config(result, oc_id);
	} else {
		i = list_seq_find(config->output_configs, output_name_cmp, "*");
		if (i >= 0) {
			// No name or identifier config, but there is a wildcard config
			free(result->name);
			result->name = strdup("*");
			merge_output_config(result, config->output_configs->items[i]);
		} else if (!config->reloading) {
			// No name, identifier, or wildcard config. Since we are not
			// reloading with defaults, the output config will be empty, so
			// just return NULL
			free_output_config(result);
			result = NULL;
		}
	}

	free(id_on_name);
	return result;
}

struct output_config *find_output_config(struct hayward_output *output) {
	char id[128];
	output_get_identifier(id, sizeof(id), output);
	return get_output_config(id, output);
}

void apply_output_config_to_outputs(struct output_config *oc) {
	// Try to find the output container and apply configuration now. If
	// this is during startup then there will be no container and config
	// will be applied during normal "new output" event from wlroots.
	bool wildcard = strcmp(oc->name, "*") == 0;
	char id[128];
	struct hayward_output *hayward_output, *tmp;
	wl_list_for_each_safe(hayward_output, tmp, &root->all_outputs, link) {
		char *name = hayward_output->wlr_output->name;
		output_get_identifier(id, sizeof(id), hayward_output);
		if (wildcard || !strcmp(name, oc->name) || !strcmp(id, oc->name)) {
			struct output_config *current =
				get_output_config(id, hayward_output);
			if (!current) {
				// No stored output config matched, apply oc directly
				hayward_log(HAYWARD_DEBUG, "Applying oc directly");
				current = new_output_config(oc->name);
				merge_output_config(current, oc);
			}
			apply_output_config(current, hayward_output);
			free_output_config(current);

			if (!wildcard) {
				// Stop looking if the output config isn't applicable to all
				// outputs
				break;
			}
		}
	}

	struct hayward_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
		cursor_rebase(seat->cursor);
	}
}

void reset_outputs(void) {
	struct output_config *oc = NULL;
	int i = list_seq_find(config->output_configs, output_name_cmp, "*");
	if (i >= 0) {
		oc = config->output_configs->items[i];
	} else {
		oc = store_output_config(new_output_config("*"));
	}
	apply_output_config_to_outputs(oc);
}

void free_output_config(struct output_config *oc) {
	if (!oc) {
		return;
	}
	free(oc->name);
	free(oc->background);
	free(oc->background_option);
	free(oc);
}

static void
handle_haywardbg_client_destroy(struct wl_listener *listener, void *data) {
	struct hayward_config *hayward_config =
		wl_container_of(listener, hayward_config, haywardbg_client_destroy);
	wl_list_remove(&hayward_config->haywardbg_client_destroy.link);
	wl_list_init(&hayward_config->haywardbg_client_destroy.link);
	hayward_config->haywardbg_client = NULL;
}

static bool _spawn_haywardbg(char **command) {
	if (config->haywardbg_client != NULL) {
		wl_client_destroy(config->haywardbg_client);
	}
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
		hayward_log_errno(HAYWARD_ERROR, "socketpair failed");
		return false;
	}
	if (!hayward_set_cloexec(sockets[0], true) ||
		!hayward_set_cloexec(sockets[1], true)) {
		return false;
	}

	config->haywardbg_client = wl_client_create(server.wl_display, sockets[0]);
	if (config->haywardbg_client == NULL) {
		hayward_log_errno(HAYWARD_ERROR, "wl_client_create failed");
		return false;
	}

	config->haywardbg_client_destroy.notify = handle_haywardbg_client_destroy;
	wl_client_add_destroy_listener(
		config->haywardbg_client, &config->haywardbg_client_destroy
	);

	pid_t pid = fork();
	if (pid < 0) {
		hayward_log_errno(HAYWARD_ERROR, "fork failed");
		return false;
	} else if (pid == 0) {
		restore_nofile_limit();

		pid = fork();
		if (pid < 0) {
			hayward_log_errno(HAYWARD_ERROR, "fork failed");
			_exit(EXIT_FAILURE);
		} else if (pid == 0) {
			if (!hayward_set_cloexec(sockets[1], false)) {
				_exit(EXIT_FAILURE);
			}

			char wayland_socket_str[16];
			snprintf(
				wayland_socket_str, sizeof(wayland_socket_str), "%d", sockets[1]
			);
			setenv("WAYLAND_SOCKET", wayland_socket_str, true);

			execvp(command[0], command);
			hayward_log_errno(HAYWARD_ERROR, "execvp failed");
			_exit(EXIT_FAILURE);
		}
		_exit(EXIT_SUCCESS);
	}

	if (close(sockets[1]) != 0) {
		hayward_log_errno(HAYWARD_ERROR, "close failed");
		return false;
	}
	if (waitpid(pid, NULL, 0) < 0) {
		hayward_log_errno(HAYWARD_ERROR, "waitpid failed");
		return false;
	}

	return true;
}

bool spawn_haywardbg(void) {
	if (!config->haywardbg_command) {
		return true;
	}

	size_t length = 2;
	for (int i = 0; i < config->output_configs->length; i++) {
		struct output_config *oc = config->output_configs->items[i];
		if (!oc->background) {
			continue;
		}
		if (strcmp(oc->background_option, "solid_color") == 0) {
			length += 4;
		} else if (oc->background_fallback) {
			length += 8;
		} else {
			length += 6;
		}
	}

	char **cmd = calloc(length, sizeof(char *));
	if (!cmd) {
		hayward_log(
			HAYWARD_ERROR, "Failed to allocate spawn_haywardbg command"
		);
		return false;
	}

	size_t i = 0;
	cmd[i++] = config->haywardbg_command;
	for (int j = 0; j < config->output_configs->length; j++) {
		struct output_config *oc = config->output_configs->items[j];
		if (!oc->background) {
			continue;
		}
		if (strcmp(oc->background_option, "solid_color") == 0) {
			cmd[i++] = "-o";
			cmd[i++] = oc->name;
			cmd[i++] = "-c";
			cmd[i++] = oc->background;
		} else {
			cmd[i++] = "-o";
			cmd[i++] = oc->name;
			cmd[i++] = "-i";
			cmd[i++] = oc->background;
			cmd[i++] = "-m";
			cmd[i++] = oc->background_option;
			if (oc->background_fallback) {
				cmd[i++] = "-c";
				cmd[i++] = oc->background_fallback;
			}
		}
		assert(i <= length);
	}

	for (size_t k = 0; k < i; k++) {
		hayward_log(HAYWARD_DEBUG, "spawn_haywardbg cmd[%zd] = %s", k, cmd[k]);
	}

	bool result = _spawn_haywardbg(cmd);
	free(cmd);
	return result;
}
