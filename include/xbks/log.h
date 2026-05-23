#ifndef XBKS_LOG_H
#define XBKS_LOG_H

#include <xbks/types.h>

enum xbks_log_level {
    XBKS_LOG_DEBUG,
    XBKS_LOG_INFO,
    XBKS_LOG_WARN,
    XBKS_LOG_ERROR,
};

void xbks_log_init(void);
void xbks_log_write(enum xbks_log_level level, const char *message);
void xbks_log_printf(enum xbks_log_level level, const char *fmt, ...);
void xbks_console_write(const char *data, size_t length);
void xbks_console_write_string(const char *text);

#endif
