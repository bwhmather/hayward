#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wlr/util/log.h>

#include <hayward/config.h>
#include <hayward/server.h>
#include <hayward/stringop.h>

struct cmd_results *
cmd_exec_validate(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, argv[-1], EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    if (!config->active || config->validating) {
        return cmd_results_new(CMD_DEFER, NULL);
    }
    return error;
}

struct cmd_results *
cmd_exec_process(int argc, char **argv) {
    struct cmd_results *error = NULL;
    char *cmd = NULL;
    if (strcmp(argv[0], "--no-startup-id") == 0) {
        wlr_log(WLR_INFO, "exec switch '--no-startup-id' not supported, ignored.");
        --argc;
        ++argv;
        if ((error = checkarg(argc, argv[-1], EXPECTED_AT_LEAST, 1))) {
            return error;
        }
    }

    if (argc == 1 && (argv[0][0] == '\'' || argv[0][0] == '"')) {
        cmd = strdup(argv[0]);
        strip_quotes(cmd);
    } else {
        cmd = join_args(argv, argc);
    }

    wlr_log(WLR_DEBUG, "Executing %s", cmd);

    int fd[2];
    if (pipe(fd) != 0) {
        wlr_log(WLR_ERROR, "Unable to create pipe for fork");
    }

    pid_t pid, child;
    // Fork process
    if ((pid = fork()) == 0) {
        // Fork child process again
        restore_nofile_limit();
        setsid();
        sigset_t set;
        sigemptyset(&set);
        sigprocmask(SIG_SETMASK, &set, NULL);
        signal(SIGPIPE, SIG_DFL);
        close(fd[0]);
        if ((child = fork()) == 0) {
            close(fd[1]);
            execlp("sh", "sh", "-c", cmd, (void *)NULL);
            wlr_log_errno(WLR_ERROR, "execlp failed");
            _exit(1);
        }
        ssize_t s = 0;
        while ((size_t)s < sizeof(pid_t)) {
            s += write(fd[1], ((uint8_t *)&child) + s, sizeof(pid_t) - s);
        }
        close(fd[1]);
        _exit(0); // Close child process
    } else if (pid < 0) {
        free(cmd);
        close(fd[0]);
        close(fd[1]);
        return cmd_results_new(CMD_FAILURE, "fork() failed");
    }
    free(cmd);
    close(fd[1]); // close write
    ssize_t s = 0;
    while ((size_t)s < sizeof(pid_t)) {
        s += read(fd[0], ((uint8_t *)&child) + s, sizeof(pid_t) - s);
    }
    close(fd[0]);
    // cleanup child process
    waitpid(pid, NULL, 0);
    if (child > 0) {
        wlr_log(WLR_DEBUG, "Child process created with pid %d", child);
    } else {
        return cmd_results_new(CMD_FAILURE, "Second fork() failed");
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *
cmd_exec_always(int argc, char **argv) {
    struct cmd_results *error;
    if ((error = cmd_exec_validate(argc, argv))) {
        return error;
    }
    return cmd_exec_process(argc, argv);
}
