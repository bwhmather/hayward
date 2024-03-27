#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include <errno.h>
#include <getopt.h>
#include <pango/pangocairo.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <wlr/version.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/haywardnag.h>
#include <hayward/profiler.h>
#include <hayward/server.h>
#include <hayward/theme.h>
#include <hayward/tree/root.h>
#include <hayward/tree/workspace.h>

static bool terminate_request = false;
static int exit_value = 0;
static struct rlimit original_nofile_rlimit = {0};
struct hwd_server server = {0};
struct hwd_debug debug = {0};

void
hwd_terminate(int exit_code) {
    if (!server.wl_display) {
        // Running as IPC client
        exit(exit_code);
    } else {
        // Running as server
        terminate_request = true;
        exit_value = exit_code;
        wl_display_terminate(server.wl_display);
    }
}

static void
sig_handler(int signal) {
    hwd_terminate(EXIT_SUCCESS);
}

static void
detect_proprietary(int allow_unsupported_gpu) {
    FILE *f = fopen("/proc/modules", "r");
    if (!f) {
        return;
    }
    char *line = NULL;
    size_t line_size = 0;
    while (getline(&line, &line_size, f) != -1) {
        if (strncmp(line, "nvidia ", 7) == 0) {
            if (allow_unsupported_gpu) {
                wlr_log(WLR_ERROR, "!!! Proprietary Nvidia drivers are in use !!!");
            } else {
                wlr_log(
                    WLR_ERROR,
                    "Proprietary Nvidia drivers are NOT supported. "
                    "Use Nouveau. To launch hayward anyway, launch with "
                    "--unsupported-gpu and DO NOT report issues."
                );
                exit(EXIT_FAILURE);
            }
            break;
        }
        if (strstr(line, "fglrx")) {
            if (allow_unsupported_gpu) {
                wlr_log(WLR_ERROR, "!!! Proprietary AMD drivers are in use !!!");
            } else {
                wlr_log(
                    WLR_ERROR,
                    "Proprietary AMD drivers do NOT support "
                    "Wayland. Use radeon. To try anyway, launch hayward with "
                    "--unsupported-gpu and DO NOT report issues."
                );
                exit(EXIT_FAILURE);
            }
            break;
        }
    }
    free(line);
    fclose(f);
}

static void
log_env(void) {
    const char *log_vars[] = {
        "LD_LIBRARY_PATH",
        "LD_PRELOAD",
        "PATH",
        "HAYWARDSOCK",
    };
    for (size_t i = 0; i < sizeof(log_vars) / sizeof(char *); ++i) {
        char *value = getenv(log_vars[i]);
        wlr_log(WLR_INFO, "%s=%s", log_vars[i], value != NULL ? value : "");
    }
}

static void
log_file(FILE *f) {
    char *line = NULL;
    size_t line_size = 0;
    ssize_t nread;
    while ((nread = getline(&line, &line_size, f)) != -1) {
        if (line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }
        wlr_log(WLR_INFO, "%s", line);
    }
    free(line);
}

static void
log_distro(void) {
    const char *paths[] = {
        "/etc/lsb-release",    "/etc/os-release",     "/etc/debian_version",
        "/etc/redhat-release", "/etc/gentoo-release",
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(char *); ++i) {
        FILE *f = fopen(paths[i], "r");
        if (f) {
            wlr_log(WLR_INFO, "Contents of %s:", paths[i]);
            log_file(f);
            fclose(f);
        }
    }
}

static void
log_kernel(void) {
    FILE *f = popen("uname -a", "r");
    if (!f) {
        wlr_log(WLR_INFO, "Unable to determine kernel version");
        return;
    }
    log_file(f);
    pclose(f);
}

static bool
drop_permissions(void) {
    if (getuid() != geteuid() || getgid() != getegid()) {
        wlr_log(
            WLR_ERROR,
            "!!! DEPRECATION WARNING: "
            "SUID privilege drop will be removed in a future release, please "
            "migrate to seatd-launch"
        );

        // Set the gid and uid in the correct order.
        if (setgid(getgid()) != 0) {
            wlr_log(WLR_ERROR, "Unable to drop root group, refusing to start");
            return false;
        }
        if (setuid(getuid()) != 0) {
            wlr_log(WLR_ERROR, "Unable to drop root user, refusing to start");
            return false;
        }
    }
    if (setgid(0) != -1 || setuid(0) != -1) {
        wlr_log(
            WLR_ERROR,
            "Unable to drop root (we shouldn't be able to "
            "restore it after setuid), refusing to start"
        );
        return false;
    }
    return true;
}

static void
increase_nofile_limit(void) {
    if (getrlimit(RLIMIT_NOFILE, &original_nofile_rlimit) != 0) {
        wlr_log_errno(
            WLR_ERROR,
            "Failed to bump max open files limit: "
            "getrlimit(NOFILE) failed"
        );
        return;
    }

    struct rlimit new_rlimit = original_nofile_rlimit;
    new_rlimit.rlim_cur = new_rlimit.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &new_rlimit) != 0) {
        wlr_log_errno(
            WLR_ERROR,
            "Failed to bump max open files limit: "
            "setrlimit(NOFILE) failed"
        );
        wlr_log(WLR_INFO, "Running with %d max open files", (int)original_nofile_rlimit.rlim_cur);
    }
}

void
restore_nofile_limit(void) {
    if (original_nofile_rlimit.rlim_cur == 0) {
        return;
    }
    if (setrlimit(RLIMIT_NOFILE, &original_nofile_rlimit) != 0) {
        wlr_log_errno(
            WLR_ERROR,
            "Failed to restore max open files limit: "
            "setrlimit(NOFILE) failed"
        );
    }
}

static void
enable_debug_flag(const char *flag) {
    if (strcmp(flag, "noatomic") == 0) {
        debug.noatomic = true;
    } else if (strcmp(flag, "txn-wait") == 0) {
        debug.txn_wait = true;
    } else if (strcmp(flag, "profile") == 0) {
        hwd_profiler_init();
    } else if (strncmp(flag, "txn-timeout=", 12) == 0) {
        server.txn_timeout_ms = atoi(&flag[12]);
    } else {
        wlr_log(WLR_ERROR, "Unknown debug flag: %s", flag);
    }
}

static const struct option long_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"config", required_argument, NULL, 'c'},
    {"validate", no_argument, NULL, 'C'},
    {"debug", no_argument, NULL, 'd'},
    {"version", no_argument, NULL, 'v'},
    {"verbose", no_argument, NULL, 'V'},
    {"get-socketpath", no_argument, NULL, 'p'},
    {"unsupported-gpu", no_argument, NULL, 'u'},
    {0, 0, 0, 0}
};

static const char usage[] = "Usage: hayward [options]\n"
                            "\n"
                            "  -h, --help             Show help message and quit.\n"
                            "  -c, --config <config>  Specify a config file.\n"
                            "  -C, --validate         Check the validity of the config file, then "
                            "exit.\n"
                            "  -d, --debug            Enables full logging, including debug "
                            "information.\n"
                            "  -v, --version          Show the version number and quit.\n"
                            "  -V, --verbose          Enables more verbose logging.\n"
                            "      --get-socketpath   Gets the IPC socket path and prints it, then "
                            "exits.\n"
                            "\n";

int
main(int argc, char **argv) {
    static bool verbose = false, debug = false, validate = false, allow_unsupported_gpu = false;

    char *config_path = NULL;

    int c;
    while (1) {
        int option_index = 0;
        c = getopt_long(argc, argv, "hCdD:vVc:", long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'h': // help
            printf("%s", usage);
            exit(EXIT_SUCCESS);
            break;
        case 'c': // config
            free(config_path);
            config_path = strdup(optarg);
            break;
        case 'C': // validate
            validate = true;
            break;
        case 'd': // debug
            debug = true;
            break;
        case 'D': // extended debug options
            enable_debug_flag(optarg);
            break;
        case 'u':
            allow_unsupported_gpu = true;
            break;
        case 'v': // version
            printf("hayward version " HWD_VERSION "\n");
            exit(EXIT_SUCCESS);
            break;
        case 'V': // verbose
            verbose = true;
            break;
        case 'p':; // --get-socketpath
            if (getenv("HAYWARDSOCK")) {
                printf("%s\n", getenv("HAYWARDSOCK"));
                exit(EXIT_SUCCESS);
            } else {
                fprintf(stderr, "hayward socket not detected.\n");
                exit(EXIT_FAILURE);
            }
            break;
        default:
            fprintf(stderr, "%s", usage);
            exit(EXIT_FAILURE);
        }
    }

    // Since wayland requires XDG_RUNTIME_DIR to be set, abort with just the
    // clear error message (when not running as an IPC client).
    if (!getenv("XDG_RUNTIME_DIR") && optind == argc) {
        fprintf(stderr, "XDG_RUNTIME_DIR is not set in the environment. Aborting.\n");
        exit(EXIT_FAILURE);
    }

    // As the 'callback' function for wlr_log is equivalent to that for
    // hayward, we do not need to override it.
    if (debug) {
        wlr_log_init(WLR_DEBUG, NULL);
    } else if (verbose) {
        wlr_log_init(WLR_INFO, NULL);
    } else {
        wlr_log_init(WLR_ERROR, NULL);
    }

    const char *wlr_v_str = WLR_VERSION_STR;
    wlr_log(WLR_INFO, "Hayward version " HWD_VERSION);
    wlr_log(WLR_INFO, "wlroots version %s", wlr_v_str);
    log_kernel();
    log_distro();
    log_env();

    detect_proprietary(allow_unsupported_gpu);

    if (!server_privileged_prepare(&server)) {
        return 1;
    }

    if (!drop_permissions()) {
        server_fini(&server);
        exit(EXIT_FAILURE);
    }

    increase_nofile_limit();

    // handle SIGTERM signals
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    // prevent ipc from crashing hayward
    signal(SIGPIPE, SIG_IGN);

    wlr_log(WLR_INFO, "Starting hayward version " HWD_VERSION);

    root = root_create(server.wl_display);

    if (!server_init(&server)) {
        return 1;
    }

    if (server.linux_dmabuf_v1) {
        wlr_scene_set_linux_dmabuf_v1(root->root_scene, server.linux_dmabuf_v1);
    }

    if (validate) {
        bool valid = load_main_config(config_path, false, true);
        free(config_path);
        return valid ? 0 : 1;
    }

    setenv("WAYLAND_DISPLAY", server.socket, true);
    if (!load_main_config(config_path, false, false)) {
        hwd_terminate(EXIT_FAILURE);
        goto shutdown;
    }

    root_set_theme(root, hwd_theme_create_default());

    // TODO this probably shouldn't live here
    char *workspace_name = "1";
    struct hwd_workspace *workspace = workspace_create(workspace_name);
    root_add_workspace(root, workspace);

    if (!server_start(&server)) {
        hwd_terminate(EXIT_FAILURE);
        goto shutdown;
    }

    config->active = true;
    run_deferred_commands();
    run_deferred_bindings();

    if (config->haywardnag_config_errors.client != NULL) {
        haywardnag_show(&config->haywardnag_config_errors);
    }

    server_run(&server);

shutdown:
    wlr_log(WLR_INFO, "Shutting down hayward");

    server_fini(&server);
    root_destroy(root);
    root = NULL;

    free(config_path);
    free_config(config);

    pango_cairo_font_map_set_default(NULL);

    return exit_value;
}
