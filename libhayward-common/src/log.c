#define _POSIX_C_SOURCE 200112L
#include "hayward-common/log.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static bool colored = true;
static hayward_log_importance_t log_importance = HAYWARD_ERROR;
static struct timespec start_time = {-1, -1};

static const char *verbosity_headers_plain[] = {
	[HAYWARD_SILENT] = "",
	[HAYWARD_ERROR] = "[ERROR]",
	[HAYWARD_INFO] = "[INFO]",
	[HAYWARD_DEBUG] = "[DEBUG]",
};

static const char *verbosity_headers_colour[] = {
	[HAYWARD_SILENT] = "",
	[HAYWARD_ERROR] = "[\x1B[1;31mERROR\x1B[0m]",
	[HAYWARD_INFO] = "[\x1B[1;34mINFO\x1B[0m]",
	[HAYWARD_DEBUG] = "[\x1B[1;90mDEBUG\x1B[0m]",
};

static void hayward_print_verbosity_stderr(hayward_log_importance_t verbosity) {
	if (colored && isatty(STDERR_FILENO)) {
		fprintf(stderr, "%s ", verbosity_headers_colour[verbosity]);
	} else {
		fprintf(stderr, "%s ", verbosity_headers_plain[verbosity]);
	}
}

static void init_start_time(void) {
	if (start_time.tv_sec >= 0) {
		return;
	}
	clock_gettime(CLOCK_MONOTONIC, &start_time);
}

static void timespec_sub(
	struct timespec *r, const struct timespec *a, const struct timespec *b
) {
	const long NSEC_PER_SEC = 1000000000;
	r->tv_sec = a->tv_sec - b->tv_sec;
	r->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += NSEC_PER_SEC;
	}
}

static void hayward_print_timestamp_stderr(void) {
	struct timespec ts = {0};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	timespec_sub(&ts, &ts, &start_time);

	fprintf(
		stderr, "%02d:%02d:%02d.%03ld ", (int)(ts.tv_sec / 60 / 60),
		(int)(ts.tv_sec / 60 % 60), (int)(ts.tv_sec % 60), ts.tv_nsec / 1000000
	);
}

static void hayward_print_location_stderr(
	const char *filename, long int lineno, const char *function
) {
	if (function != NULL) {
		fprintf(stderr, "[%s:%ld:%s] ", filename, lineno, function);
	} else {
		fprintf(stderr, "[%s:%ld] ", filename, lineno);
	}
}

void hayward_log_init(hayward_log_importance_t verbosity) {
	init_start_time();

	if (verbosity < HAYWARD_LOG_IMPORTANCE_LAST) {
		log_importance = verbosity;
	}
}

void _hayward_vlog(
	hayward_log_importance_t verbosity, const char *filename, long int lineno,
	const char *format, va_list args
) {
	init_start_time();

	if (verbosity > log_importance) {
		return;
	}

	hayward_print_verbosity_stderr(verbosity);
	hayward_print_timestamp_stderr();
	hayward_print_location_stderr(filename, lineno, NULL);

	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
}

void _hayward_log(
	hayward_log_importance_t verbosity, const char *filename, long int lineno,
	const char *format, ...
) {
	va_list args;
	va_start(args, format);
	_hayward_vlog(verbosity, filename, lineno, format, args);
	va_end(args);
}

void _hayward_vlog_errno(
	hayward_log_importance_t verbosity, const char *filename, long int lineno,
	const char *format, va_list args
) {
	init_start_time();

	if (verbosity > log_importance) {
		return;
	}

	hayward_print_verbosity_stderr(verbosity);
	hayward_print_timestamp_stderr();
	hayward_print_location_stderr(filename, lineno, NULL);

	vfprintf(stderr, format, args);
	fprintf(stderr, ": %s\n", strerror(errno));
}

void _hayward_log_errno(
	hayward_log_importance_t verbosity, const char *filename, long int lineno,
	const char *format, ...
) {
	va_list args;
	va_start(args, format);
	_hayward_vlog(verbosity, filename, lineno, format, args);
	va_end(args);
}

noreturn void
_hayward_abort(const char *filename, long int lineno, const char *format, ...) {
	va_list args;
	va_start(args, format);
	_hayward_vlog(HAYWARD_ERROR, filename, lineno, format, args);
	va_end(args);

	raise(SIGABRT);
	exit(1);
}

void _hayward_assert(
	bool condition, const char *filename, long int lineno, const char *function,
	const char *format, ...
) {
	if (condition) {
		return;
	}

	hayward_print_verbosity_stderr(HAYWARD_ERROR);
	hayward_print_timestamp_stderr();
	hayward_print_location_stderr(filename, lineno, function);

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	fprintf(stderr, "\n");

	raise(SIGABRT);
	exit(1);
}
