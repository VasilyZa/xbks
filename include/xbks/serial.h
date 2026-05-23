#ifndef XBKS_SERIAL_H
#define XBKS_SERIAL_H

#include <xbks/types.h>

#define XBKS_SERIAL_COM1 ((uint16_t)0x3f8)

bool xbks_serial_init(uint16_t port);
bool xbks_serial_read_byte(uint16_t port, uint8_t *out);
void xbks_serial_write_byte(uint16_t port, uint8_t byte);
void xbks_serial_write(uint16_t port, const char *data, size_t length);
void xbks_serial_write_string(uint16_t port, const char *text);

#endif
