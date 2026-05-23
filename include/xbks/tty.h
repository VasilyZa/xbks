#ifndef XBKS_TTY_H
#define XBKS_TTY_H

#include <limine.h>
#include <xbks/types.h>

bool xbks_tty_init(
    const struct limine_framebuffer_response *framebuffer,
    const struct limine_module_response *modules
);
bool xbks_tty_is_available(void);
void xbks_tty_write(const char *data, size_t length);
void xbks_tty_write_string(const char *text);

#endif
