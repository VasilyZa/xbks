#include <stdarg.h>
#include <xbks/format.h>
#include <xbks/io.h>
#include <xbks/log.h>
#include <xbks/serial.h>
#include <xbks/string.h>
#include <xbks/tty.h>

static const char *level_name(enum xbks_log_level level) {
    switch (level) {
    case XBKS_LOG_DEBUG:
        return "debug";
    case XBKS_LOG_INFO:
        return "info";
    case XBKS_LOG_WARN:
        return "warn";
    case XBKS_LOG_ERROR:
        return "error";
    }

    return "unknown";
}

void xbks_log_init(void) {
    (void)xbks_serial_init(XBKS_SERIAL_COM1);
}

void xbks_console_write(const char *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        xbks_debugcon_write_byte((uint8_t)data[i]);
    }

    xbks_serial_write(XBKS_SERIAL_COM1, data, length);
    xbks_tty_write(data, length);
}

void xbks_console_write_string(const char *text) {
    xbks_console_write(text, strlen(text));
}

void xbks_log_write(enum xbks_log_level level, const char *message) {
    xbks_console_write_string("[");
    xbks_console_write_string(level_name(level));
    xbks_console_write_string("] ");
    xbks_console_write_string(message);
    xbks_console_write_string("\n");
}

void xbks_log_printf(enum xbks_log_level level, const char *fmt, ...) {
    char buffer[512];
    va_list args;

    va_start(args, fmt);
    (void)xbks_vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    xbks_log_write(level, buffer);
}
