#define _POSIX_C_SOURCE 200112L
#include "hayward/log.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static bool colored = true;
static hwd_log_importance_t log_importance = HWD_ERROR;
static struct timespec start_time = {-1, -1};

static const char *verbosity_headers_plain[] = {
    [HWD_SILENT] = "",
    [HWD_ERROR] = "[ERROR]",
    [HWD_INFO] = "[INFO]",
    [HWD_DEBUG] = "[DEBUG]",
};

static const char *verbosity_headers_colour[] = {
    [HWD_SILENT] = "",
    [HWD_ERROR] = "[\x1B[1;31mERROR\x1B[0m]",
    [HWD_INFO] = "[\x1B[1;34mINFO\x1B[0m]",
    [HWD_DEBUG] = "[\x1B[1;90mDEBUG\x1B[0m]",
};

static void
hwd_print_verbosity_stderr(hwd_log_importance_t verbosity) {
    if (colored && isatty(STDERR_FILENO)) {
        fprintf(stderr, "%s ", verbosity_headers_colour[verbosity]);
    } else {
        fprintf(stderr, "%s ", verbosity_headers_plain[verbosity]);
    }
}

static void
init_start_time(void) {
    if (start_time.tv_sec >= 0) {
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &start_time);
}

static void
timespec_sub(struct timespec *r, const struct timespec *a, const struct timespec *b) {
    const long NSEC_PER_SEC = 1000000000;
    r->tv_sec = a->tv_sec - b->tv_sec;
    r->tv_nsec = a->tv_nsec - b->tv_nsec;
    if (r->tv_nsec < 0) {
        r->tv_sec--;
        r->tv_nsec += NSEC_PER_SEC;
    }
}

static void
hwd_print_timestamp_stderr(void) {
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    timespec_sub(&ts, &ts, &start_time);

    fprintf(
        stderr, "%02d:%02d:%02d.%03ld ", (int)(ts.tv_sec / 60 / 60), (int)(ts.tv_sec / 60 % 60),
        (int)(ts.tv_sec % 60), ts.tv_nsec / 1000000
    );
}

static void
hwd_print_location_stderr(const char *filename, long int lineno, const char *function) {
    if (function != NULL) {
        fprintf(stderr, "[%s:%ld:%s] ", filename, lineno, function);
    } else {
        fprintf(stderr, "[%s:%ld] ", filename, lineno);
    }
}

void
hwd_log_init(hwd_log_importance_t verbosity) {
    init_start_time();

    if (verbosity < HWD_LOG_IMPORTANCE_LAST) {
        log_importance = verbosity;
    }
}

void
_hwd_vlog(
    hwd_log_importance_t verbosity, const char *filename, long int lineno, const char *function,
    const char *format, va_list args
) {
    init_start_time();

    if (verbosity > log_importance) {
        return;
    }

    hwd_print_verbosity_stderr(verbosity);
    hwd_print_timestamp_stderr();
    hwd_print_location_stderr(filename, lineno, function);

    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
}

void
_hwd_log(
    hwd_log_importance_t verbosity, const char *filename, long int lineno, const char *function,
    const char *format, ...
) {
    va_list args;
    va_start(args, format);
    _hwd_vlog(verbosity, filename, lineno, function, format, args);
    va_end(args);
}

void
_hwd_vlog_errno(
    hwd_log_importance_t verbosity, const char *filename, long int lineno, const char *function,
    const char *format, va_list args
) {
    init_start_time();

    if (verbosity > log_importance) {
        return;
    }

    hwd_print_verbosity_stderr(verbosity);
    hwd_print_timestamp_stderr();
    hwd_print_location_stderr(filename, lineno, function);

    vfprintf(stderr, format, args);
    fprintf(stderr, ": %s\n", strerror(errno));
}

void
_hwd_log_errno(
    hwd_log_importance_t verbosity, const char *filename, long int lineno, const char *function,
    const char *format, ...
) {
    va_list args;
    va_start(args, format);
    _hwd_vlog(verbosity, filename, lineno, function, format, args);
    va_end(args);
}

noreturn void
_hwd_abort(const char *filename, long int lineno, const char *function, const char *format, ...) {
    va_list args;
    va_start(args, format);
    _hwd_vlog(HWD_ERROR, filename, lineno, function, format, args);
    va_end(args);

    raise(SIGABRT);
    exit(1);
}

void
_hwd_assert(
    bool condition, const char *filename, long int lineno, const char *function, const char *format,
    ...
) {
    if (condition) {
        return;
    }

    hwd_print_verbosity_stderr(HWD_ERROR);
    hwd_print_timestamp_stderr();
    hwd_print_location_stderr(filename, lineno, function);

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fprintf(stderr, "\n");

    raise(SIGABRT);
    exit(1);
}
