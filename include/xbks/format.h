#ifndef XBKS_FORMAT_H
#define XBKS_FORMAT_H

#include <stdarg.h>
#include <xbks/types.h>

int xbks_vsnprintf(char *buffer, size_t buffer_size, const char *fmt, va_list args);
int xbks_snprintf(char *buffer, size_t buffer_size, const char *fmt, ...);

#endif
