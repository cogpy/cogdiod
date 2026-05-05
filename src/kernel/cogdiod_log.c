/* cogdiod_log.c — Levelled logging implementation */
#include "cogdiod_log.h"
#include <stdarg.h>
#include <stdlib.h>

static CogLogLevel log_min_level = LOG_INFO;
static FILE*       log_file      = NULL;

static const char* level_tag[] = {
    [LOG_DEBUG] = "DBG",
    [LOG_INFO]  = "INF",
    [LOG_WARN]  = "WRN",
    [LOG_ERROR] = "ERR",
    [LOG_FATAL] = "FTL",
};

void cogdiod_log_set_level(CogLogLevel level) {
    log_min_level = level;
}

void cogdiod_log_set_file(FILE* f) {
    log_file = f;
}

void cogdiod_log(CogLogLevel level, const char* file, int line,
                 const char* fmt, ...) {
    if (level < log_min_level) return;

    FILE* out = log_file ? log_file : stderr;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long ms = ts.tv_nsec / 1000000;

    fprintf(out, "[%s %ld.%03ld %s:%d] ",
            level_tag[level], (long)ts.tv_sec, ms, file, line);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
    fflush(out);

    if (level == LOG_FATAL) abort();
}
