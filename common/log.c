#define _POSIX_C_SOURCE 200112L
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "log.h"

static terminate_callback_t log_terminate = exit;

static bool colored = true;
static wmiiv_log_importance_t log_importance = WMIIV_ERROR;
static struct timespec start_time = {-1, -1};

static const char *verbosity_headers_plain[] = {
	[WMIIV_SILENT] = "",
	[WMIIV_ERROR] = "[ERROR]",
	[WMIIV_INFO] = "[INFO]",
	[WMIIV_DEBUG] = "[DEBUG]",
};

static const char *verbosity_headers_colour[] = {
	[WMIIV_SILENT] = "",
	[WMIIV_ERROR ] = "[\x1B[1;31mERROR\x1B[0m]",
	[WMIIV_INFO  ] = "[\x1B[1;34mINFO\x1B[0m]",
	[WMIIV_DEBUG ] = "[\x1B[1;90mDEBUG\x1B[0m]",
};

static void wmiiv_print_verbosity_stderr(wmiiv_log_importance_t verbosity) {
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

static void timespec_sub(struct timespec *r, const struct timespec *a,
		const struct timespec *b) {
	const long NSEC_PER_SEC = 1000000000;
	r->tv_sec = a->tv_sec - b->tv_sec;
	r->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += NSEC_PER_SEC;
	}
}

static void wmiiv_print_timestamp_stderr(void) {
	struct timespec ts = {0};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	timespec_sub(&ts, &ts, &start_time);

	fprintf(stderr, "%02d:%02d:%02d.%03ld ", (int)(ts.tv_sec / 60 / 60),
		(int)(ts.tv_sec / 60 % 60), (int)(ts.tv_sec % 60),
		ts.tv_nsec / 1000000);
}

static void wmiiv_print_location_stderr(const char *filename, long int lineno, const char *function) {
	if (function != NULL) {
		fprintf(stderr, "[%s:%ld:%s] ", filename, lineno, function);
	} else {
		fprintf(stderr, "[%s:%ld] ", filename, lineno);
	}

}

void wmiiv_log_init(wmiiv_log_importance_t verbosity, terminate_callback_t callback) {
	init_start_time();

	if (verbosity < WMIIV_LOG_IMPORTANCE_LAST) {
		log_importance = verbosity;
	}
	if (callback) {
		log_terminate = callback;
	}
}

void _wmiiv_vlog(wmiiv_log_importance_t verbosity, const char *filename, long int lineno, const char *format, va_list args) {
	init_start_time();

	if (verbosity > log_importance) {
		return;
	}

	wmiiv_print_verbosity_stderr(verbosity);
	wmiiv_print_timestamp_stderr();
	wmiiv_print_location_stderr(filename, lineno, NULL);

	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
}

void _wmiiv_log(wmiiv_log_importance_t verbosity, const char *filename, long int lineno, const char *format, ...) {
	va_list args;
	va_start(args, format);
	_wmiiv_vlog(verbosity, filename, lineno, format, args);
	va_end(args);
}

void _wmiiv_vlog_errno(wmiiv_log_importance_t verbosity, const char *filename, long int lineno, const char *format, va_list args) {
	init_start_time();

	if (verbosity > log_importance) {
		return;
	}

	wmiiv_print_verbosity_stderr(verbosity);
	wmiiv_print_timestamp_stderr();
	wmiiv_print_location_stderr(filename, lineno, NULL);

	vfprintf(stderr, format, args);
	fprintf(stderr, ": %s\n", strerror(errno));
}

void _wmiiv_log_errno(wmiiv_log_importance_t verbosity, const char *filename, long int lineno, const char *format, ...) {
	va_list args;
	va_start(args, format);
	_wmiiv_vlog(verbosity, filename, lineno, format, args);
	va_end(args);
}

void _wmiiv_abort(const char *filename, long int lineno, const char *format, ...) {
	va_list args;
	va_start(args, format);
	_wmiiv_vlog(WMIIV_ERROR, filename, lineno, format, args);
	va_end(args);
	log_terminate(EXIT_FAILURE);
}

bool _wmiiv_assert(bool condition, const char *filename, long int lineno, const char *function, const char *format, ...) {
	if (condition) {
		return true;
	}

	wmiiv_print_verbosity_stderr(WMIIV_ERROR);
	wmiiv_print_timestamp_stderr();
	wmiiv_print_location_stderr(filename, lineno, function);

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	fprintf(stderr, "\n");

#ifndef NDEBUG
	raise(SIGABRT);
#endif

	return false;
}

