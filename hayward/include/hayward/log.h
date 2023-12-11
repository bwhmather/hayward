#ifndef _HWD_LOG_H
#define _HWD_LOG_H

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <string.h>

typedef enum {
    HWD_SILENT = 0,
    HWD_ERROR = 1,
    HWD_INFO = 2,
    HWD_DEBUG = 3,
    HWD_LOG_IMPORTANCE_LAST,
} hwd_log_importance_t;

#ifdef __GNUC__
#define ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define ATTRIB_PRINTF(start, end)
#endif

// Will log all messages less than or equal to `verbosity`
// The `terminate` callback is called by `hwd_abort`
void
hwd_log_init(hwd_log_importance_t verbosity);

void
_hwd_vlog(
    hwd_log_importance_t verbosity, const char *filename, long int lineno, const char *function,
    const char *format, va_list args
) ATTRIB_PRINTF(5, 0);
void
_hwd_log(
    hwd_log_importance_t verbosity, const char *filename, long int lineno, const char *function,
    const char *format, ...
) ATTRIB_PRINTF(5, 6);
void
_hwd_vlog_errno(
    hwd_log_importance_t verbosity, const char *filename, long int lineno, const char *function,
    const char *format, va_list args
) ATTRIB_PRINTF(5, 0);
void
_hwd_log_errno(
    hwd_log_importance_t verbosity, const char *filename, long int lineno, const char *function,
    const char *format, ...
) ATTRIB_PRINTF(5, 6);
noreturn void
_hwd_abort(const char *filename, long int lineno, const char *function, const char *format, ...)
    ATTRIB_PRINTF(4, 5);
void
_hwd_assert(
    bool condition, const char *filename, long int lineno, const char *function, const char *format,
    ...
) ATTRIB_PRINTF(5, 6);

#ifdef HWD_REL_SRC_DIR
// strip prefix from __FILE__, leaving the path relative to the project root
#define _HWD_FILENAME ((const char *)__FILE__ + sizeof(HWD_REL_SRC_DIR) - 1)
#else
#define _HWD_FILENAME __FILE__
#endif

#define hwd_log(VERB, ...) _hwd_log(VERB, _HWD_FILENAME, __LINE__, __func__, ##__VA_ARGS__)

#define hwd_vlog(VERB, FMT, ARGS) _hwd_vlog(VERB, _HWD_FILENAME, __LINE__, __func__, FMT, ARGS)

#define hwd_log_errno(VERB, ...)                                                                   \
    _hwd_log_errno(VERB, _HWD_FILENAME, __LINE__, __func__, ##__VA_ARGS__)

#define hwd_abort(...) _hwd_abort(_HWD_FILENAME, __LINE__, __func__, ##__VA_ARGS__)

#define hwd_assert(COND, ...) _hwd_assert(COND, _HWD_FILENAME, __LINE__, __func__, ##__VA_ARGS__)

#endif
