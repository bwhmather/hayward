#ifndef _WMIIV_LOG_H
#define _WMIIV_LOG_H

#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

typedef enum {
	WMIIV_SILENT = 0,
	WMIIV_ERROR = 1,
	WMIIV_INFO = 2,
	WMIIV_DEBUG = 3,
	WMIIV_LOG_IMPORTANCE_LAST,
} wmiiv_log_importance_t;

#ifdef __GNUC__
#define ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define ATTRIB_PRINTF(start, end)
#endif

void error_handler(int sig);

typedef void (*terminate_callback_t)(int exit_code);

// Will log all messages less than or equal to `verbosity`
// The `terminate` callback is called by `wmiiv_abort`
void wmiiv_log_init(wmiiv_log_importance_t verbosity, terminate_callback_t terminate);

void _wmiiv_vlog(wmiiv_log_importance_t verbosity, const char *filename, long int lineno, const char *format, va_list args) ATTRIB_PRINTF(4, 0);
void _wmiiv_log(wmiiv_log_importance_t verbosity, const char *filename, long int lineno, const char *format, ...) ATTRIB_PRINTF(4, 5);
void _wmiiv_vlog_errno(wmiiv_log_importance_t verbosity, const char *filename, long int lineno, const char *format, va_list args) ATTRIB_PRINTF(4, 0);
void _wmiiv_log_errno(wmiiv_log_importance_t verbosity, const char *filename, long int lineno, const char *format, ...) ATTRIB_PRINTF(4, 5);
void _wmiiv_abort(const char *filename, long int lineno, const char *format, ...) ATTRIB_PRINTF(3, 4);
bool _wmiiv_assert(bool condition, const char *filename, long int lineno, const char *function, const char* format, ...) ATTRIB_PRINTF(5, 6);

#ifdef WMIIV_REL_SRC_DIR
// strip prefix from __FILE__, leaving the path relative to the project root
#define _WMIIV_FILENAME ((const char *)__FILE__ + sizeof(WMIIV_REL_SRC_DIR) - 1)
#else
#define _WMIIV_FILENAME __FILE__
#endif

#define wmiiv_log(VERB, ...) \
	_wmiiv_log(VERB,  _WMIIV_FILENAME, __LINE__, ##__VA_ARGS__)

#define wmiiv_vlog(VERB, FMT, ARGS) \
	_wmiiv_vlog(VERB, _WMIIV_FILENAME, __LINE__, FMT, ARGS)

#define wmiiv_log_errno(VERB, ...) \
	_wmiiv_log_errno(VERB, _WMIIV_FILENAME, __LINE__, ##__VA_ARGS__)

#define wmiiv_abort(...) \
	_wmiiv_abort(_WMIIV_FILENAME,  __LINE__, ##__VA_ARGS__)

#define wmiiv_assert(COND, ...) \
	_wmiiv_assert(COND, _WMIIV_FILENAME, __LINE__, __func__, ##__VA_ARGS__)

#endif
