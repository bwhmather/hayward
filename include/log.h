#ifndef _HAYWARD_LOG_H
#define _HAYWARD_LOG_H

#include <stdnoreturn.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

typedef enum {
	HAYWARD_SILENT = 0,
	HAYWARD_ERROR = 1,
	HAYWARD_INFO = 2,
	HAYWARD_DEBUG = 3,
	HAYWARD_LOG_IMPORTANCE_LAST,
} hayward_log_importance_t;

#ifdef __GNUC__
#define ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define ATTRIB_PRINTF(start, end)
#endif

// Will log all messages less than or equal to `verbosity`
// The `terminate` callback is called by `hayward_abort`
void hayward_log_init(hayward_log_importance_t verbosity);

void _hayward_vlog(hayward_log_importance_t verbosity, const char *filename, long int lineno, const char *format, va_list args) ATTRIB_PRINTF(4, 0);
void _hayward_log(hayward_log_importance_t verbosity, const char *filename, long int lineno, const char *format, ...) ATTRIB_PRINTF(4, 5);
void _hayward_vlog_errno(hayward_log_importance_t verbosity, const char *filename, long int lineno, const char *format, va_list args) ATTRIB_PRINTF(4, 0);
void _hayward_log_errno(hayward_log_importance_t verbosity, const char *filename, long int lineno, const char *format, ...) ATTRIB_PRINTF(4, 5);
noreturn void _hayward_abort(const char *filename, long int lineno, const char *format, ...) ATTRIB_PRINTF(3, 4);
void _hayward_assert(bool condition, const char *filename, long int lineno, const char *function, const char* format, ...) ATTRIB_PRINTF(5, 6);

#ifdef HAYWARD_REL_SRC_DIR
// strip prefix from __FILE__, leaving the path relative to the project root
#define _HAYWARD_FILENAME ((const char *)__FILE__ + sizeof(HAYWARD_REL_SRC_DIR) - 1)
#else
#define _HAYWARD_FILENAME __FILE__
#endif

#define hayward_log(VERB, ...) \
	_hayward_log(VERB,  _HAYWARD_FILENAME, __LINE__, ##__VA_ARGS__)

#define hayward_vlog(VERB, FMT, ARGS) \
	_hayward_vlog(VERB, _HAYWARD_FILENAME, __LINE__, FMT, ARGS)

#define hayward_log_errno(VERB, ...) \
	_hayward_log_errno(VERB, _HAYWARD_FILENAME, __LINE__, ##__VA_ARGS__)

#define hayward_abort(...) \
	_hayward_abort(_HAYWARD_FILENAME,  __LINE__, ##__VA_ARGS__)

#define hayward_assert(COND, ...) \
	_hayward_assert(COND, _HAYWARD_FILENAME, __LINE__, __func__, ##__VA_ARGS__)

#endif
