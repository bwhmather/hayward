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
#include "hayward/server.h"
#include "hayward/haywardnag.h"
#include "util.h"

static void handle_haywardnag_client_destroy(struct wl_listener *listener,
		void *data) {
	struct haywardnag_instance *haywardnag =
		wl_container_of(listener, haywardnag, client_destroy);
	wl_list_remove(&haywardnag->client_destroy.link);
	wl_list_init(&haywardnag->client_destroy.link);
	haywardnag->client = NULL;
}

bool haywardnag_spawn(const char *haywardnag_command,
		struct haywardnag_instance *haywardnag) {
	if (haywardnag->client != NULL) {
		wl_client_destroy(haywardnag->client);
	}

	if (!haywardnag_command) {
		return true;
	}

	if (haywardnag->detailed) {
		if (pipe(haywardnag->fd) != 0) {
			hayward_log(HAYWARD_ERROR, "Failed to create pipe for haywardnag");
			return false;
		}
		if (!hayward_set_cloexec(haywardnag->fd[1], true)) {
			goto failed;
		}
	}

	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
		hayward_log_errno(HAYWARD_ERROR, "socketpair failed");
		goto failed;
	}
	if (!hayward_set_cloexec(sockets[0], true) || !hayward_set_cloexec(sockets[1], true)) {
		goto failed;
	}

	haywardnag->client = wl_client_create(server.wl_display, sockets[0]);
	if (haywardnag->client == NULL) {
		hayward_log_errno(HAYWARD_ERROR, "wl_client_create failed");
		goto failed;
	}

	haywardnag->client_destroy.notify = handle_haywardnag_client_destroy;
	wl_client_add_destroy_listener(haywardnag->client, &haywardnag->client_destroy);

	pid_t pid = fork();
	if (pid < 0) {
		hayward_log(HAYWARD_ERROR, "Failed to create fork for haywardnag");
		goto failed;
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

			if (haywardnag->detailed) {
				close(haywardnag->fd[1]);
				dup2(haywardnag->fd[0], STDIN_FILENO);
				close(haywardnag->fd[0]);
			}

			char wayland_socket_str[16];
			snprintf(wayland_socket_str, sizeof(wayland_socket_str),
					"%d", sockets[1]);
			setenv("WAYLAND_SOCKET", wayland_socket_str, true);

			size_t length = strlen(haywardnag_command) + strlen(haywardnag->args) + 2;
			char *cmd = malloc(length);
			snprintf(cmd, length, "%s %s", haywardnag_command, haywardnag->args);
			execlp("sh", "sh", "-c", cmd, NULL);
			hayward_log_errno(HAYWARD_ERROR, "execlp failed");
			_exit(EXIT_FAILURE);
		}
		_exit(EXIT_SUCCESS);
	}

	if (haywardnag->detailed) {
		if (close(haywardnag->fd[0]) != 0) {
			hayward_log_errno(HAYWARD_ERROR, "close failed");
			return false;
		}
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

failed:
	if (haywardnag->detailed) {
		if (close(haywardnag->fd[0]) != 0) {
			hayward_log_errno(HAYWARD_ERROR, "close failed");
			return false;
		}
		if (close(haywardnag->fd[1]) != 0) {
			hayward_log_errno(HAYWARD_ERROR, "close failed");
		}
	}
	return false;
}

void haywardnag_log(const char *haywardnag_command, struct haywardnag_instance *haywardnag,
		const char *fmt, ...) {
	if (!haywardnag_command) {
		return;
	}

	if (!haywardnag->detailed) {
		hayward_log(HAYWARD_ERROR, "Attempting to write to non-detailed haywardnag inst");
		return;
	}

	if (haywardnag->client == NULL && !haywardnag_spawn(haywardnag_command, haywardnag)) {
		return;
	}

	va_list args;
	va_start(args, fmt);
	size_t length = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);

	char *temp = malloc(length + 1);
	if (!temp) {
		hayward_log(HAYWARD_ERROR, "Failed to allocate buffer for haywardnag log entry.");
		return;
	}

	va_start(args, fmt);
	vsnprintf(temp, length, fmt, args);
	va_end(args);

	write(haywardnag->fd[1], temp, length);

	free(temp);
}

void haywardnag_show(struct haywardnag_instance *haywardnag) {
	if (haywardnag->detailed && haywardnag->client != NULL) {
		close(haywardnag->fd[1]);
	}
}

