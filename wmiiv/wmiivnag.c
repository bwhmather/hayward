#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "log.h"
#include "wmiiv/server.h"
#include "wmiiv/wmiivnag.h"
#include "util.h"

static void handle_wmiivnag_client_destroy(struct wl_listener *listener,
		void *data) {
	struct wmiivnag_instance *wmiivnag =
		wl_container_of(listener, wmiivnag, client_destroy);
	wl_list_remove(&wmiivnag->client_destroy.link);
	wl_list_init(&wmiivnag->client_destroy.link);
	wmiivnag->client = NULL;
}

bool wmiivnag_spawn(const char *wmiivnag_command,
		struct wmiivnag_instance *wmiivnag) {
	if (wmiivnag->client != NULL) {
		wl_client_destroy(wmiivnag->client);
	}

	if (!wmiivnag_command) {
		return true;
	}

	if (wmiivnag->detailed) {
		if (pipe(wmiivnag->fd) != 0) {
			wmiiv_log(SWAY_ERROR, "Failed to create pipe for wmiivnag");
			return false;
		}
		if (!wmiiv_set_cloexec(wmiivnag->fd[1], true)) {
			goto failed;
		}
	}

	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
		wmiiv_log_errno(SWAY_ERROR, "socketpair failed");
		goto failed;
	}
	if (!wmiiv_set_cloexec(sockets[0], true) || !wmiiv_set_cloexec(sockets[1], true)) {
		goto failed;
	}

	wmiivnag->client = wl_client_create(server.wl_display, sockets[0]);
	if (wmiivnag->client == NULL) {
		wmiiv_log_errno(SWAY_ERROR, "wl_client_create failed");
		goto failed;
	}

	wmiivnag->client_destroy.notify = handle_wmiivnag_client_destroy;
	wl_client_add_destroy_listener(wmiivnag->client, &wmiivnag->client_destroy);

	pid_t pid = fork();
	if (pid < 0) {
		wmiiv_log(SWAY_ERROR, "Failed to create fork for wmiivnag");
		goto failed;
	} else if (pid == 0) {
		restore_nofile_limit();

		pid = fork();
		if (pid < 0) {
			wmiiv_log_errno(SWAY_ERROR, "fork failed");
			_exit(EXIT_FAILURE);
		} else if (pid == 0) {
			if (!wmiiv_set_cloexec(sockets[1], false)) {
				_exit(EXIT_FAILURE);
			}

			if (wmiivnag->detailed) {
				close(wmiivnag->fd[1]);
				dup2(wmiivnag->fd[0], STDIN_FILENO);
				close(wmiivnag->fd[0]);
			}

			char wayland_socket_str[16];
			snprintf(wayland_socket_str, sizeof(wayland_socket_str),
					"%d", sockets[1]);
			setenv("WAYLAND_SOCKET", wayland_socket_str, true);

			size_t length = strlen(wmiivnag_command) + strlen(wmiivnag->args) + 2;
			char *cmd = malloc(length);
			snprintf(cmd, length, "%s %s", wmiivnag_command, wmiivnag->args);
			execlp("sh", "sh", "-c", cmd, NULL);
			wmiiv_log_errno(SWAY_ERROR, "execlp failed");
			_exit(EXIT_FAILURE);
		}
		_exit(EXIT_SUCCESS);
	}

	if (wmiivnag->detailed) {
		if (close(wmiivnag->fd[0]) != 0) {
			wmiiv_log_errno(SWAY_ERROR, "close failed");
			return false;
		}
	}

	if (close(sockets[1]) != 0) {
		wmiiv_log_errno(SWAY_ERROR, "close failed");
		return false;
	}

	if (waitpid(pid, NULL, 0) < 0) {
		wmiiv_log_errno(SWAY_ERROR, "waitpid failed");
		return false;
	}

	return true;

failed:
	if (wmiivnag->detailed) {
		if (close(wmiivnag->fd[0]) != 0) {
			wmiiv_log_errno(SWAY_ERROR, "close failed");
			return false;
		}
		if (close(wmiivnag->fd[1]) != 0) {
			wmiiv_log_errno(SWAY_ERROR, "close failed");
		}
	}
	return false;
}

void wmiivnag_log(const char *wmiivnag_command, struct wmiivnag_instance *wmiivnag,
		const char *fmt, ...) {
	if (!wmiivnag_command) {
		return;
	}

	if (!wmiivnag->detailed) {
		wmiiv_log(SWAY_ERROR, "Attempting to write to non-detailed wmiivnag inst");
		return;
	}

	if (wmiivnag->client == NULL && !wmiivnag_spawn(wmiivnag_command, wmiivnag)) {
		return;
	}

	va_list args;
	va_start(args, fmt);
	size_t length = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);

	char *temp = malloc(length + 1);
	if (!temp) {
		wmiiv_log(SWAY_ERROR, "Failed to allocate buffer for wmiivnag log entry.");
		return;
	}

	va_start(args, fmt);
	vsnprintf(temp, length, fmt, args);
	va_end(args);

	write(wmiivnag->fd[1], temp, length);

	free(temp);
}

void wmiivnag_show(struct wmiivnag_instance *wmiivnag) {
	if (wmiivnag->detailed && wmiivnag->client != NULL) {
		close(wmiivnag->fd[1]);
	}
}

