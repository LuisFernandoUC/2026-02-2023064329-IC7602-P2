/*
 * log.c - Implementación de la bitácora.
 */
#include "log.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static log_level_t current_level = ZC_LOG_INFO;

void log_set_level(log_level_t level)
{
    current_level = level;
}

log_level_t log_level_from_string(const char *s)
{
    if (!s)
        return ZC_LOG_INFO;
    if (zc_strcaseeq(s, "debug"))
        return ZC_LOG_DEBUG;
    if (zc_strcaseeq(s, "warn") || zc_strcaseeq(s, "warning"))
        return ZC_LOG_WARN;
    if (zc_strcaseeq(s, "error"))
        return ZC_LOG_ERROR;
    return ZC_LOG_INFO;
}

void log_msg(log_level_t level, const char *fmt, ...)
{
    static const char *names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    if (level < current_level)
        return;

    char stamp[32];
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm);

    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s [%s] %s\n", stamp, names[level], msg);
}
