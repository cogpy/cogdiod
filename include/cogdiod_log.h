/* cogdiod_log.h — Levelled logging for CogDiod */
#pragma once
#include <stdio.h>
#include <time.h>

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3,
    LOG_FATAL = 4,
} CogLogLevel;

/* Set minimum level; messages below this are dropped */
void cogdiod_log_set_level(CogLogLevel level);

/* Set output file (default: stderr) */
void cogdiod_log_set_file(FILE* f);

/* Low-level log function */
void cogdiod_log(CogLogLevel level, const char* file, int line,
                 const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

#define LOG_D(fmt, ...) cogdiod_log(LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) cogdiod_log(LOG_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) cogdiod_log(LOG_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) cogdiod_log(LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_F(fmt, ...) cogdiod_log(LOG_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
