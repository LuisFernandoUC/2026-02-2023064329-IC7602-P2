/*
 * log.h - Bitácora simple a stderr con nivel configurable (ZC_LOG_LEVEL).
 */
#ifndef ZC_LOG_H
#define ZC_LOG_H

typedef enum { LOG_DEBUG = 0, LOG_INFO, LOG_WARN, LOG_ERROR } log_level_t;

void log_set_level(log_level_t level);
/* "debug", "info", "warn", "error" -> nivel. Devuelve LOG_INFO si no se reconoce. */
log_level_t log_level_from_string(const char *s);
void log_msg(log_level_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define LOG_D(...) log_msg(LOG_DEBUG, __VA_ARGS__)
#define LOG_I(...) log_msg(LOG_INFO, __VA_ARGS__)
#define LOG_W(...) log_msg(LOG_WARN, __VA_ARGS__)
#define LOG_E(...) log_msg(LOG_ERROR, __VA_ARGS__)

#endif
