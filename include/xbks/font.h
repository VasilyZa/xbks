#ifndef XBKS_FONT_H
#define XBKS_FONT_H

#include <limine.h>
#include <xbks/types.h>

enum {
    XBKS_TTY_FONT_GLYPHS = 256,
    XBKS_TTY_FONT_WIDTH = 13,
    XBKS_TTY_FONT_HEIGHT = 28,
};

struct xbks_bitmap_font {
    uint8_t *alpha;
    size_t glyphs;
    size_t width;
    size_t height;
    const char *source;
};

bool xbks_font_load_system_tty(const struct limine_module_response *modules, struct xbks_bitmap_font *out);

#endif
