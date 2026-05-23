#ifndef XBKS_KEYBOARD_H
#define XBKS_KEYBOARD_H

#include <xbks/types.h>

void xbks_keyboard_init(void);
bool xbks_keyboard_read_char(char *out);
char xbks_keyboard_read_char_blocking(void);

#endif
