/*
 * cogdiod_log.h — Structured logging for CogDiod
 */
#pragma once
#include <stdbool.h>

typedef enum { LOG_DEBUG=0, LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

void cogdiod_log(LogLevel level, const char* module, const char* fmt, ...);
void cogdiod_log_set_level(LogLevel min_level);
void cogdiod_log_set_json(bool json_output);
