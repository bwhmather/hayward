#ifndef HWD_PROFILER_H
#define HWD_PROFILER_H

#ifdef HAVE_SYSPROF
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <sysprof-capture.h>
#pragma GCC diagnostic pop
#endif

typedef uint64_t hwd_timestamp;

struct hwd_profiler_span {
    hwd_timestamp begin;
    const char *message;
};

static inline void
hwd_profiler_init(void) {
#ifdef HAVE_SYSPROF
    sysprof_collector_init();
#endif
}

static inline hwd_timestamp
hwd_profiler_now(void) {
#ifdef HAVE_SYSPROF
    return SYSPROF_CAPTURE_CURRENT_TIME;
#else
    return 0;
#endif
}

static inline void
hwd_profiler_mark(const char *message, hwd_timestamp begin, hwd_timestamp end) {
#ifdef HAVE_SYSPROF
    sysprof_collector_mark(begin, end - begin, "hwd", message, NULL);
#else
    (void)message;
    (void)begin;
    (void)end;
#endif
}

#define HWD_PROFILER_TRACE_SPAN_NAME_INNER_(func, line) hwd_profiler_span_##func##_##line
#define HWD_PROFILER_TRACE_SPAN_NAME_(func, line) HWD_PROFILER_TRACE_SPAN_NAME_INNER_(func, line)
#define HWD_PROFILER_TRACE()                                                                       \
    __attribute__((cleanup(hwd_profiler_span_cleanup__))                                           \
    ) struct hwd_profiler_span HWD_PROFILER_TRACE_SPAN_NAME_(__func__, __LINE__) = {               \
        .begin = hwd_profiler_now(), .message = __func__};                                         \
    (void)HWD_PROFILER_TRACE_SPAN_NAME_(__func__, __LINE__)

static inline void
hwd_profiler_span_cleanup__(struct hwd_profiler_span *span) {
    hwd_profiler_mark(span->message, span->begin, hwd_profiler_now());
}

#endif
