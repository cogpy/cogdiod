/*
 * cogdiod_log.c — Structured logging for CogDiod (Item 30)
 *
 * Supports plain-text and JSON-lines output, configurable log level.
 */

#include "cogdiod_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

static LogLevel  g_level    = LOG_INFO;
static bool      g_json     = false;

void cogdiod_log_set_level(LogLevel level) { g_level = level; }
void cogdiod_log_set_json(bool enable)     { g_json  = enable; }

static const char* level_str(LogLevel l) {
    switch (l) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARN";
    case LOG_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

void cogdiod_log(LogLevel level, const char* component,
                 const char* fmt, ...) {
    if (level < g_level) return;

    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    if (g_json) {
        fprintf(stderr,
            "{\"ts\":%lld,\"level\":\"%s\",\"component\":\"%s\",\"msg\":\"%s\"}\n",
            (long long)ts.tv_sec, level_str(level), component, msg);
    } else {
        fprintf(stderr, "[%s][%s] %s\n",
                level_str(level), component, msg);
    }
}