#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_buffer.h>
#include "wmiiv/config.h"
#include "wmiiv/desktop.h"
#include "wmiiv/desktop/idle_inhibit_v1.h"
#include "wmiiv/desktop/transaction.h"
#include "wmiiv/input/cursor.h"
#include "wmiiv/input/input-manager.h"
#include "wmiiv/output.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/node.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/workspace.h"
#include "list.h"
#include "log.h"

struct wmiiv_transaction {
	struct wl_event_source *timer;
	list_t *instructions;   // struct wmiiv_transaction_instruction *
	size_t num_waiting;
	size_t num_configures;
	struct timespec commit_time;
};

struct wmiiv_transaction_instruction {
	struct wmiiv_transaction *transaction;
	struct wmiiv_node *node;
	union {
		struct wmiiv_output_state output_state;
		struct wmiiv_workspace_state workspace_state;
		struct wmiiv_container_state container_state;
	};
	uint32_t serial;
	bool server_request;
	bool waiting;
};

static struct wmiiv_transaction *transaction_create(void) {
	struct wmiiv_transaction *transaction =
		calloc(1, sizeof(struct wmiiv_transaction));
	if (!wmiiv_assert(transaction, "Unable to allocate transaction")) {
		return NULL;
	}
	transaction->instructions = create_list();
	return transaction;
}

static void transaction_destroy(struct wmiiv_transaction *transaction) {
	// Free instructions
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct wmiiv_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct wmiiv_node *node = instruction->node;
		node->ntxnrefs--;
		if (node->instruction == instruction) {
			node->instruction = NULL;
		}
		if (node->destroying && node->ntxnrefs == 0) {
			switch (node->type) {
			case N_ROOT:
				wmiiv_assert(false, "Never reached");
				break;
			case N_OUTPUT:
				output_destroy(node->wmiiv_output);
				break;
			case N_WORKSPACE:
				workspace_destroy(node->wmiiv_workspace);
				break;
			case N_COLUMN:
				column_destroy(node->wmiiv_column);
				break;
			case N_WINDOW:
				window_destroy(node->wmiiv_window);
				break;
			}
		}
		free(instruction);
	}
	list_free(transaction->instructions);

	if (transaction->timer) {
		wl_event_source_remove(transaction->timer);
	}
	free(transaction);
}

static void copy_output_state(struct wmiiv_output *output,
		struct wmiiv_transaction_instruction *instruction) {
	struct wmiiv_output_state *state = &instruction->output_state;
	if (state->workspaces) {
		state->workspaces->length = 0;
	} else {
		state->workspaces = create_list();
	}
	list_cat(state->workspaces, output->workspaces);

	state->active_workspace = output_get_active_workspace(output);
}

static void copy_workspace_state(struct wmiiv_workspace *workspace,
		struct wmiiv_transaction_instruction *instruction) {
	struct wmiiv_workspace_state *state = &instruction->workspace_state;

	state->fullscreen = workspace->fullscreen;
	state->x = workspace->x;
	state->y = workspace->y;
	state->width = workspace->width;
	state->height = workspace->height;

	state->output = workspace->output;
	if (state->floating) {
		state->floating->length = 0;
	} else {
		state->floating = create_list();
	}
	if (state->tiling) {
		state->tiling->length = 0;
	} else {
		state->tiling = create_list();
	}
	list_cat(state->floating, workspace->floating);
	list_cat(state->tiling, workspace->tiling);

	struct wmiiv_seat *seat = input_manager_current_seat();
	state->focused = seat_get_focus(seat) == &workspace->node;

	// Set focused_inactive_child to the direct tiling child
	struct wmiiv_container *focus = seat_get_focus_inactive_tiling(seat, workspace);
	state->focused_inactive_child = focus ? focus->pending.parent : NULL;
}

static void copy_column_state(struct wmiiv_column *container,
		struct wmiiv_transaction_instruction *instruction) {
	if (!wmiiv_assert(container_is_column(container), "Expected column")) {
		return;
	}

	struct wmiiv_container_state *state = &instruction->container_state;

	if (state->children) {
		list_free(state->children);
	}

	memcpy(state, &container->pending, sizeof(struct wmiiv_container_state));

	// We store a copy of the child list to avoid having it mutated after
	// we copy the state.
	state->children = create_list();
	list_cat(state->children, container->pending.children);

	struct wmiiv_seat *seat = input_manager_current_seat();
	state->focused = seat_get_focus(seat) == &container->node;

	struct wmiiv_node *focus =
		seat_get_active_tiling_child(seat, &container->node);
	state->focused_inactive_child = focus ? focus->wmiiv_window : NULL;
}

static void copy_window_state(struct wmiiv_container *container,
		struct wmiiv_transaction_instruction *instruction) {
	struct wmiiv_container_state *state = &instruction->container_state;

	memcpy(state, &container->pending, sizeof(struct wmiiv_container_state));
	state->children = NULL;

	struct wmiiv_seat *seat = input_manager_current_seat();
	state->focused = seat_get_focus(seat) == &container->node;
}

static void transaction_add_node(struct wmiiv_transaction *transaction,
		struct wmiiv_node *node, bool server_request) {
	struct wmiiv_transaction_instruction *instruction = NULL;

	// Check if we have an instruction for this node already, in which case we
	// update that instead of creating a new one.
	if (node->ntxnrefs > 0) {
		for (int idx = 0; idx < transaction->instructions->length; idx++) {
			struct wmiiv_transaction_instruction *other =
				transaction->instructions->items[idx];
			if (other->node == node) {
				instruction = other;
				break;
			}
		}
	}

	if (!instruction) {
		instruction = calloc(1, sizeof(struct wmiiv_transaction_instruction));
		if (!wmiiv_assert(instruction, "Unable to allocate instruction")) {
			return;
		}
		instruction->transaction = transaction;
		instruction->node = node;
		instruction->server_request = server_request;

		list_add(transaction->instructions, instruction);
		node->ntxnrefs++;
	} else if (server_request) {
		instruction->server_request = true;
	}

	switch (node->type) {
	case N_ROOT:
		break;
	case N_OUTPUT:
		copy_output_state(node->wmiiv_output, instruction);
		break;
	case N_WORKSPACE:
		copy_workspace_state(node->wmiiv_workspace, instruction);
		break;
	case N_COLUMN:
		copy_column_state(node->wmiiv_column, instruction);
		break;
	case N_WINDOW:
		copy_window_state(node->wmiiv_window, instruction);
		break;
	}
}

static void apply_output_state(struct wmiiv_output *output,
		struct wmiiv_output_state *state) {
	output_damage_whole(output);
	list_free(output->current.workspaces);
	memcpy(&output->current, state, sizeof(struct wmiiv_output_state));
	output_damage_whole(output);
}

static void apply_workspace_state(struct wmiiv_workspace *workspace,
		struct wmiiv_workspace_state *state) {
	output_damage_whole(workspace->current.output);
	list_free(workspace->current.floating);
	list_free(workspace->current.tiling);
	memcpy(&workspace->current, state, sizeof(struct wmiiv_workspace_state));
	output_damage_whole(workspace->current.output);
}

static void apply_column_state(struct wmiiv_column *column, struct wmiiv_container_state *state) {
	// Damage the old location
	desktop_damage_column(column);

	// There are separate children lists for each instruction state, the
	// container's current state and the container's pending state
	// (ie. column->children). The list itself needs to be freed here.
	// Any child containers which are being deleted will be cleaned up in
	// transaction_destroy().
	list_free(column->current.children);

	memcpy(&column->current, state, sizeof(struct wmiiv_container_state));

	// Damage the new location
	desktop_damage_column(column);

	if (!column->node.destroying) {
		column_discover_outputs(column);
	}
}

static void apply_window_state(struct wmiiv_container *window,
		struct wmiiv_container_state *state) {
	struct wmiiv_view *view = window->view;
	// Damage the old location
	desktop_damage_window(window);
	if (!wl_list_empty(&view->saved_buffers)) {
		struct wmiiv_saved_buffer *saved_buf;
		wl_list_for_each(saved_buf, &view->saved_buffers, link) {
			struct wlr_box box = {
				.x = saved_buf->x - view->saved_geometry.x,
				.y = saved_buf->y - view->saved_geometry.y,
				.width = saved_buf->width,
				.height = saved_buf->height,
			};
			desktop_damage_box(&box);
		}
	}

	// There are separate children lists for each instruction state, the
	// window's current state and the window's pending state.  The list
	// itself needs to be freed here.  Any child containers which are being
	// deleted will be cleaned up in transaction_destroy().
	list_free(window->current.children);

	memcpy(&window->current, state, sizeof(struct wmiiv_container_state));

	if (!wl_list_empty(&view->saved_buffers)) {
		if (!window->node.destroying || window->node.ntxnrefs == 1) {
			view_remove_saved_buffer(view);
		}
	}

	// If the view hasn't responded to the configure, center it within
	// the window. This is important for fullscreen views which
	// refuse to resize to the size of the output.
	if (view->surface) {
		view_center_surface(view);
	}

	// Damage the new location
	desktop_damage_window(window);
	if (view->surface) {
		struct wlr_surface *surface = view->surface;
		struct wlr_box box = {
			.x = window->current.content_x - view->geometry.x,
			.y = window->current.content_y - view->geometry.y,
			.width = surface->current.width,
			.height = surface->current.height,
		};
		desktop_damage_box(&box);
	}

	if (!window->node.destroying) {
		window_discover_outputs(window);
	}
}

/**
 * Apply a transaction to the "current" state of the tree.
 */
static void transaction_apply(struct wmiiv_transaction *transaction) {
	wmiiv_log(WMIIV_DEBUG, "Applying transaction %p", transaction);
	if (debug.txn_timings) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct timespec *commit = &transaction->commit_time;
		float ms = (now.tv_sec - commit->tv_sec) * 1000 +
			(now.tv_nsec - commit->tv_nsec) / 1000000.0;
		wmiiv_log(WMIIV_DEBUG, "Transaction %p: %.1fms waiting "
				"(%.1f frames if 60Hz)", transaction, ms, ms / (1000.0f / 60));
	}

	// Apply the instruction state to the node's current state
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct wmiiv_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct wmiiv_node *node = instruction->node;

		switch (node->type) {
		case N_ROOT:
			break;
		case N_OUTPUT:
			apply_output_state(node->wmiiv_output, &instruction->output_state);
			break;
		case N_WORKSPACE:
			apply_workspace_state(node->wmiiv_workspace,
					&instruction->workspace_state);
			break;
		case N_COLUMN:
			apply_column_state(node->wmiiv_column,
					&instruction->container_state);
			break;
		case N_WINDOW:
			apply_window_state(node->wmiiv_window,
					&instruction->container_state);
		}

		node->instruction = NULL;
	}

	cursor_rebase_all();
}

static void transaction_commit_pending(void);

static void transaction_progress(void) {
	if (!server.queued_transaction) {
		return;
	}
	if (server.queued_transaction->num_waiting > 0) {
		return;
	}
	transaction_apply(server.queued_transaction);
	transaction_destroy(server.queued_transaction);
	server.queued_transaction = NULL;

	if (!server.pending_transaction) {
		wmiiv_idle_inhibit_v1_check_active(server.idle_inhibit_manager_v1);
		return;
	}

	transaction_commit_pending();
}

static int handle_timeout(void *data) {
	struct wmiiv_transaction *transaction = data;
	wmiiv_log(WMIIV_DEBUG, "Transaction %p timed out (%zi waiting)",
			transaction, transaction->num_waiting);
	transaction->num_waiting = 0;
	transaction_progress();
	return 0;
}

static bool should_configure(struct wmiiv_node *node,
		struct wmiiv_transaction_instruction *instruction) {
	if (!node_is_view(node)) {
		return false;
	}
	if (node->destroying) {
		return false;
	}
	if (!instruction->server_request) {
		return false;
	}
	struct wmiiv_container_state *cstate = &node->wmiiv_window->current;
	struct wmiiv_container_state *istate = &instruction->container_state;
#if HAVE_XWAYLAND
	// Xwayland views are position-aware and need to be reconfigured
	// when their position changes.
	if (node->wmiiv_window->view->type == WMIIV_VIEW_XWAYLAND) {
		// WMiiv logical coordinates are doubles, but they get truncated to
		// integers when sent to Xwayland through `xcb_configure_window`.
		// X11 apps will not respond to duplicate configure requests (from their
		// truncated point of view) and cause transactions to time out.
		if ((int)cstate->content_x != (int)istate->content_x ||
				(int)cstate->content_y != (int)istate->content_y) {
			return true;
		}
	}
#endif
	if (cstate->content_width == istate->content_width &&
			cstate->content_height == istate->content_height) {
		return false;
	}
	return true;
}

static void transaction_commit(struct wmiiv_transaction *transaction) {
	wmiiv_log(WMIIV_DEBUG, "Transaction %p committing with %i instructions",
			transaction, transaction->instructions->length);
	transaction->num_waiting = 0;
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct wmiiv_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct wmiiv_node *node = instruction->node;
		bool hidden = node_is_view(node) && !node->destroying &&
			!view_is_visible(node->wmiiv_window->view);
		if (should_configure(node, instruction)) {
			instruction->serial = view_configure(node->wmiiv_window->view,
					instruction->container_state.content_x,
					instruction->container_state.content_y,
					instruction->container_state.content_width,
					instruction->container_state.content_height);
			if (!hidden) {
				instruction->waiting = true;
				++transaction->num_waiting;
			}

			// From here on we are rendering a saved buffer of the view, which
			// means we can send a frame done event to make the client redraw it
			// as soon as possible. Additionally, this is required if a view is
			// mapping and its default geometry doesn't intersect an output.
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			wlr_surface_send_frame_done(
					node->wmiiv_window->view->surface, &now);
		}
		if (!hidden && node_is_view(node) &&
				wl_list_empty(&node->wmiiv_window->view->saved_buffers)) {
			view_save_buffer(node->wmiiv_window->view);
			memcpy(&node->wmiiv_window->view->saved_geometry,
					&node->wmiiv_window->view->geometry,
					sizeof(struct wlr_box));
		}
		node->instruction = instruction;
	}
	transaction->num_configures = transaction->num_waiting;
	if (debug.txn_timings) {
		clock_gettime(CLOCK_MONOTONIC, &transaction->commit_time);
	}
	if (debug.noatomic) {
		transaction->num_waiting = 0;
	} else if (debug.txn_wait) {
		// Force the transaction to time out even if all views are ready.
		// We do this by inflating the waiting counter.
		transaction->num_waiting += 1000000;
	}

	if (transaction->num_waiting) {
		// Set up a timer which the views must respond within
		transaction->timer = wl_event_loop_add_timer(server.wl_event_loop,
				handle_timeout, transaction);
		if (transaction->timer) {
			wl_event_source_timer_update(transaction->timer,
					server.txn_timeout_ms);
		} else {
			wmiiv_log_errno(WMIIV_ERROR, "Unable to create transaction timer "
					"(some imperfect frames might be rendered)");
			transaction->num_waiting = 0;
		}
	}
}

static void transaction_commit_pending(void) {
	if (server.queued_transaction) {
		return;
	}
	struct wmiiv_transaction *transaction = server.pending_transaction;
	server.pending_transaction = NULL;
	server.queued_transaction = transaction;
	transaction_commit(transaction);
	transaction_progress();
}

static void set_instruction_ready(
		struct wmiiv_transaction_instruction *instruction) {
	struct wmiiv_transaction *transaction = instruction->transaction;
	wmiiv_assert(node_is_view(instruction->node), "Expected view");

	if (debug.txn_timings) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct timespec *start = &transaction->commit_time;
		float ms = (now.tv_sec - start->tv_sec) * 1000 +
			(now.tv_nsec - start->tv_nsec) / 1000000.0;
		wmiiv_log(WMIIV_DEBUG, "Transaction %p: %zi/%zi ready in %.1fms (%s)",
				transaction,
				transaction->num_configures - transaction->num_waiting + 1,
				transaction->num_configures, ms,
				instruction->node->wmiiv_window->title);
	}

	// If the transaction has timed out then its num_waiting will be 0 already.
	if (instruction->waiting && transaction->num_waiting > 0 &&
			--transaction->num_waiting == 0) {
		wmiiv_log(WMIIV_DEBUG, "Transaction %p is ready", transaction);
		wl_event_source_timer_update(transaction->timer, 0);
	}

	instruction->node->instruction = NULL;
	transaction_progress();
}

void transaction_notify_view_ready_by_serial(struct wmiiv_view *view,
		uint32_t serial) {
	struct wmiiv_transaction_instruction *instruction =
		view->container->node.instruction;
	if (instruction != NULL && instruction->serial == serial) {
		set_instruction_ready(instruction);
	}
}

void transaction_notify_view_ready_by_geometry(struct wmiiv_view *view,
		double x, double y, int width, int height) {
	struct wmiiv_transaction_instruction *instruction =
		view->container->node.instruction;
	if (instruction != NULL &&
			(int)instruction->container_state.content_x == (int)x &&
			(int)instruction->container_state.content_y == (int)y &&
			instruction->container_state.content_width == width &&
			instruction->container_state.content_height == height) {
		set_instruction_ready(instruction);
	}
}

static void _transaction_commit_dirty(bool server_request) {
	if (!server.dirty_nodes->length) {
		return;
	}

	if (!server.pending_transaction) {
		server.pending_transaction = transaction_create();
		if (!server.pending_transaction) {
			return;
		}
	}

	for (int i = 0; i < server.dirty_nodes->length; ++i) {
		struct wmiiv_node *node = server.dirty_nodes->items[i];
		transaction_add_node(server.pending_transaction, node, server_request);
		node->dirty = false;
	}
	server.dirty_nodes->length = 0;

	transaction_commit_pending();
}

void transaction_commit_dirty(void) {
	_transaction_commit_dirty(true);
}

void transaction_commit_dirty_client(void) {
	_transaction_commit_dirty(false);
}
