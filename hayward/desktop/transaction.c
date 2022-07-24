#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_buffer.h>
#include "hayward/config.h"
#include "hayward/desktop.h"
#include "hayward/desktop/idle_inhibit_v1.h"
#include "hayward/desktop/transaction.h"
#include "hayward/input/cursor.h"
#include "hayward/input/input-manager.h"
#include "hayward/output.h"
#include "hayward/tree/column.h"
#include "hayward/tree/window.h"
#include "hayward/tree/node.h"
#include "hayward/tree/view.h"
#include "hayward/tree/workspace.h"
#include "list.h"
#include "log.h"

struct hayward_transaction {
	struct wl_event_source *timer;
	list_t *instructions;   // struct hayward_transaction_instruction *
	size_t num_waiting;
	size_t num_configures;
	struct timespec commit_time;
};

struct hayward_transaction_instruction {
	struct hayward_transaction *transaction;
	struct hayward_node *node;
	union {
		struct hayward_root_state root_state;
		struct hayward_output_state output_state;
		struct hayward_workspace_state workspace_state;
		struct hayward_column_state column_state;
		struct hayward_window_state window_state;
	};
	uint32_t serial;
	bool server_request;
	bool waiting;
};

static struct hayward_transaction *transaction_create(void) {
	struct hayward_transaction *transaction =
		calloc(1, sizeof(struct hayward_transaction));
	hayward_assert(transaction, "Unable to allocate transaction");

	transaction->instructions = create_list();
	return transaction;
}

static void transaction_destroy(struct hayward_transaction *transaction) {
	// Free instructions
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct hayward_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct hayward_node *node = instruction->node;
		node->ntxnrefs--;
		if (node->instruction == instruction) {
			node->instruction = NULL;
		}
		if (node->destroying && node->ntxnrefs == 0) {
			switch (node->type) {
			case N_ROOT:
				hayward_abort("Never reached");
			case N_OUTPUT:
				output_destroy(node->hayward_output);
				break;
			case N_WORKSPACE:
				workspace_destroy(node->hayward_workspace);
				break;
			case N_COLUMN:
				column_destroy(node->hayward_column);
				break;
			case N_WINDOW:
				window_destroy(node->hayward_window);
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

static void copy_root_state(struct hayward_root *root, struct hayward_transaction_instruction *instruction) {
	struct hayward_root_state *state = &instruction->root_state;
	if (state->workspaces) {
		state->workspaces->length = 0;
	} else {
		state->workspaces = create_list();
	}
	list_cat(state->workspaces, root->pending.workspaces);
	state->active_workspace = root->pending.active_workspace;
}

static void copy_output_state(struct hayward_output *output, struct hayward_transaction_instruction *instruction) {
}

static void copy_workspace_state(struct hayward_workspace *workspace,
		struct hayward_transaction_instruction *instruction) {
	struct hayward_workspace_state *state = &instruction->workspace_state;

	state->fullscreen = workspace->pending.fullscreen;
	state->x = workspace->pending.x;
	state->y = workspace->pending.y;
	state->width = workspace->pending.width;
	state->height = workspace->pending.height;

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
	list_cat(state->floating, workspace->pending.floating);
	list_cat(state->tiling, workspace->pending.tiling);

	struct hayward_seat *seat = input_manager_current_seat();
	state->focused = seat_get_focus(seat) == &workspace->node;

	state->active_column = workspace->pending.active_column;
	state->focus_mode = workspace->pending.focus_mode;
}

static void copy_column_state(struct hayward_column *column,
		struct hayward_transaction_instruction *instruction) {
	struct hayward_column_state *state = &instruction->column_state;

	if (state->children) {
		list_free(state->children);
	}

	memcpy(state, &column->pending, sizeof(struct hayward_column_state));

	// We store a copy of the child list to avoid having it mutated after
	// we copy the state.
	state->children = create_list();
	list_cat(state->children, column->pending.children);

	struct hayward_seat *seat = input_manager_current_seat();
	state->focused = seat_get_focus(seat) == &column->node;

	state->active_child = column->pending.active_child;
}

static void copy_window_state(struct hayward_window *window,
		struct hayward_transaction_instruction *instruction) {
	struct hayward_window_state *state = &instruction->window_state;

	memcpy(state, &window->pending, sizeof(struct hayward_window_state));

	struct hayward_seat *seat = input_manager_current_seat();
	state->focused = seat_get_focus(seat) == &window->node;
}

static void transaction_add_node(struct hayward_transaction *transaction,
		struct hayward_node *node, bool server_request) {
	struct hayward_transaction_instruction *instruction = NULL;

	// Check if we have an instruction for this node already, in which case we
	// update that instead of creating a new one.
	if (node->ntxnrefs > 0) {
		for (int idx = 0; idx < transaction->instructions->length; idx++) {
			struct hayward_transaction_instruction *other =
				transaction->instructions->items[idx];
			if (other->node == node) {
				instruction = other;
				break;
			}
		}
	}

	if (!instruction) {
		instruction = calloc(1, sizeof(struct hayward_transaction_instruction));
		hayward_assert(instruction, "Unable to allocate instruction");

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
		copy_root_state(node->hayward_root, instruction);
		break;
	case N_OUTPUT:
		copy_output_state(node->hayward_output, instruction);
		break;
	case N_WORKSPACE:
		copy_workspace_state(node->hayward_workspace, instruction);
		break;
	case N_COLUMN:
		copy_column_state(node->hayward_column, instruction);
		break;
	case N_WINDOW:
		copy_window_state(node->hayward_window, instruction);
		break;
	}
}

static void apply_root_state(struct hayward_root *root, struct hayward_root_state *state) {
	if (root->current.active_workspace != NULL) {
		workspace_damage_whole(root->current.active_workspace);
	}
	list_free(root->current.workspaces);
	memcpy(&root->current, state, sizeof(struct hayward_output_state));
	if (root->current.active_workspace != NULL) {
		workspace_damage_whole(root->current.active_workspace);
	}
}

static void apply_output_state(struct hayward_output *output,
		struct hayward_output_state *state) {
	output_damage_whole(output);
	memcpy(&output->current, state, sizeof(struct hayward_output_state));
	output_damage_whole(output);
}

static void apply_workspace_state(struct hayward_workspace *workspace,
		struct hayward_workspace_state *state) {
	workspace_damage_whole(workspace);
	list_free(workspace->current.floating);
	list_free(workspace->current.tiling);
	memcpy(&workspace->current, state, sizeof(struct hayward_workspace_state));
	workspace_damage_whole(workspace);
}

static void apply_column_state(struct hayward_column *column, struct hayward_column_state *state) {
	// Damage the old location
	desktop_damage_column(column);

	// There are separate children lists for each instruction state, the
	// container's current state and the container's pending state
	// (ie. column->children). The list itself needs to be freed here.
	// Any child containers which are being deleted will be cleaned up in
	// transaction_destroy().
	list_free(column->current.children);

	memcpy(&column->current, state, sizeof(struct hayward_column_state));

	// Damage the new location
	desktop_damage_column(column);

	if (!column->node.destroying) {
		column_discover_outputs(column);
	}
}

static void apply_window_state(struct hayward_window *window,
		struct hayward_window_state *state) {
	struct hayward_view *view = window->view;
	// Damage the old location
	desktop_damage_window(window);
	if (!wl_list_empty(&view->saved_buffers)) {
		struct hayward_saved_buffer *saved_buf;
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

	memcpy(&window->current, state, sizeof(struct hayward_window_state));

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
static void transaction_apply(struct hayward_transaction *transaction) {
	hayward_log(HAYWARD_DEBUG, "Applying transaction %p", (void *) transaction);
	if (debug.txn_timings) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct timespec *commit = &transaction->commit_time;
		float ms = (now.tv_sec - commit->tv_sec) * 1000 +
			(now.tv_nsec - commit->tv_nsec) / 1000000.0;
		hayward_log(HAYWARD_DEBUG, "Transaction %p: %.1fms waiting "
				"(%.1f frames if 60Hz)", (void *) transaction, ms, ms / (1000.0f / 60));
	}

	// Apply the instruction state to the node's current state
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct hayward_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct hayward_node *node = instruction->node;

		switch (node->type) {
		case N_ROOT:
			apply_root_state(node->hayward_root, &instruction->root_state);
			break;
		case N_OUTPUT:
			apply_output_state(node->hayward_output, &instruction->output_state);
			break;
		case N_WORKSPACE:
			apply_workspace_state(node->hayward_workspace,
					&instruction->workspace_state);
			break;
		case N_COLUMN:
			apply_column_state(node->hayward_column,
					&instruction->column_state);
			break;
		case N_WINDOW:
			apply_window_state(node->hayward_window,
					&instruction->window_state);
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
		hayward_idle_inhibit_v1_check_active(server.idle_inhibit_manager_v1);
		return;
	}

	transaction_commit_pending();
}

static int handle_timeout(void *data) {
	struct hayward_transaction *transaction = data;
	hayward_log(HAYWARD_DEBUG, "Transaction %p timed out (%zi waiting)",
			(void *) transaction, transaction->num_waiting);
	transaction->num_waiting = 0;
	transaction_progress();
	return 0;
}

static bool should_configure(struct hayward_node *node,
		struct hayward_transaction_instruction *instruction) {
	if (!node_is_view(node)) {
		return false;
	}
	if (node->destroying) {
		return false;
	}
	if (!instruction->server_request) {
		return false;
	}
	struct hayward_window_state *cstate = &node->hayward_window->current;
	struct hayward_window_state *istate = &instruction->window_state;
#if HAVE_XWAYLAND
	// Xwayland views are position-aware and need to be reconfigured
	// when their position changes.
	if (node->hayward_window->view->type == HAYWARD_VIEW_XWAYLAND) {
		// Hayward logical coordinates are doubles, but they get truncated to
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

static void transaction_commit(struct hayward_transaction *transaction) {
	hayward_log(HAYWARD_DEBUG, "Transaction %p committing with %i instructions",
			(void *) transaction, transaction->instructions->length);
	transaction->num_waiting = 0;
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct hayward_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct hayward_node *node = instruction->node;
		bool hidden = node_is_view(node) && !node->destroying &&
			!view_is_visible(node->hayward_window->view);
		if (should_configure(node, instruction)) {
			instruction->serial = view_configure(node->hayward_window->view,
					instruction->window_state.content_x,
					instruction->window_state.content_y,
					instruction->window_state.content_width,
					instruction->window_state.content_height);
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
					node->hayward_window->view->surface, &now);
		}
		if (!hidden && node_is_view(node) &&
				wl_list_empty(&node->hayward_window->view->saved_buffers)) {
			view_save_buffer(node->hayward_window->view);
			memcpy(&node->hayward_window->view->saved_geometry,
					&node->hayward_window->view->geometry,
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
			hayward_log_errno(HAYWARD_ERROR, "Unable to create transaction timer "
					"(some imperfect frames might be rendered)");
			transaction->num_waiting = 0;
		}
	}
}

static void transaction_commit_pending(void) {
	if (server.queued_transaction) {
		return;
	}
	struct hayward_transaction *transaction = server.pending_transaction;
	server.pending_transaction = NULL;
	server.queued_transaction = transaction;
	transaction_commit(transaction);
	transaction_progress();
}

static void set_instruction_ready(
		struct hayward_transaction_instruction *instruction) {
	struct hayward_transaction *transaction = instruction->transaction;
	hayward_assert(node_is_view(instruction->node), "Expected view");

	if (debug.txn_timings) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct timespec *start = &transaction->commit_time;
		float ms = (now.tv_sec - start->tv_sec) * 1000 +
			(now.tv_nsec - start->tv_nsec) / 1000000.0;
		hayward_log(HAYWARD_DEBUG, "Transaction %p: %zi/%zi ready in %.1fms (%s)",
				(void *) transaction,
				transaction->num_configures - transaction->num_waiting + 1,
				transaction->num_configures, ms,
				instruction->node->hayward_window->title);
	}

	// If the transaction has timed out then its num_waiting will be 0 already.
	if (instruction->waiting && transaction->num_waiting > 0 &&
			--transaction->num_waiting == 0) {
		hayward_log(HAYWARD_DEBUG, "Transaction %p is ready", (void *) transaction);
		wl_event_source_timer_update(transaction->timer, 0);
	}

	instruction->node->instruction = NULL;
	transaction_progress();
}

void transaction_notify_view_ready_by_serial(struct hayward_view *view,
		uint32_t serial) {
	struct hayward_transaction_instruction *instruction =
		view->window->node.instruction;
	if (instruction != NULL && instruction->serial == serial) {
		set_instruction_ready(instruction);
	}
}

void transaction_notify_view_ready_by_geometry(struct hayward_view *view,
		double x, double y, int width, int height) {
	struct hayward_transaction_instruction *instruction =
		view->window->node.instruction;
	if (instruction != NULL &&
			(int)instruction->window_state.content_x == (int)x &&
			(int)instruction->window_state.content_y == (int)y &&
			instruction->window_state.content_width == width &&
			instruction->window_state.content_height == height) {
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
		struct hayward_node *node = server.dirty_nodes->items[i];
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
