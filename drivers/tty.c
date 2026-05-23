#include <flanterm.h>
#include <flanterm_backends/fb.h>
#include <xbks/font.h>
#include <xbks/log.h>
#include <xbks/tty.h>

static struct flanterm_context *tty_context;

bool xbks_tty_init(
    const struct limine_framebuffer_response *response,
    const struct limine_module_response *modules
) {
    if (response == 0 || response->framebuffer_count == 0 || response->framebuffers == 0) {
        return false;
    }

    struct limine_framebuffer *framebuffer = response->framebuffers[0];
    if (framebuffer == 0 || framebuffer->address == 0 || framebuffer->bpp != 32) {
        return false;
    }

    struct xbks_bitmap_font font;
    const bool use_system_font = xbks_font_load_system_tty(modules, &font);

    if (use_system_font) {
        tty_context = flanterm_fb_init_alpha(
            0,                         /* malloc */
            0,                         /* free */
            framebuffer->address,
            framebuffer->width,
            framebuffer->height,
            framebuffer->pitch,
            framebuffer->red_mask_size,
            framebuffer->red_mask_shift,
            framebuffer->green_mask_size,
            framebuffer->green_mask_shift,
            framebuffer->blue_mask_size,
            framebuffer->blue_mask_shift,
            0,                         /* canvas */
            0,                         /* ansi colours */
            0,                         /* bright ansi colours */
            0,                         /* default background */
            0,                         /* default foreground */
            0,                         /* bright default background */
            0,                         /* bright default foreground */
            font.alpha,
            font.width,
            font.height,
            1,                         /* font spacing */
            1,                         /* font scale x */
            1,                         /* font scale y */
            0,                         /* margin */
            FLANTERM_FB_ROTATE_0
        );
    }

    if (tty_context == 0) {
        tty_context = flanterm_fb_init(
            0,                         /* malloc */
            0,                         /* free */
            framebuffer->address,
            framebuffer->width,
            framebuffer->height,
            framebuffer->pitch,
            framebuffer->red_mask_size,
            framebuffer->red_mask_shift,
            framebuffer->green_mask_size,
            framebuffer->green_mask_shift,
            framebuffer->blue_mask_size,
            framebuffer->blue_mask_shift,
            0,                         /* canvas */
            0,                         /* ansi colours */
            0,                         /* bright ansi colours */
            0,                         /* default background */
            0,                         /* default foreground */
            0,                         /* bright default background */
            0,                         /* bright default foreground */
            0,                         /* font */
            0,                         /* font width */
            0,                         /* font height */
            0,                         /* font spacing */
            0,                         /* font scale x */
            0,                         /* font scale y */
            0,                         /* margin */
            FLANTERM_FB_ROTATE_0
        );
    }

    if (tty_context != 0 && use_system_font) {
        xbks_log_printf(XBKS_LOG_INFO, "TTY using TTF font module: %s", font.source);
    }

    return tty_context != 0;
}

bool xbks_tty_is_available(void) {
    return tty_context != 0;
}

void xbks_tty_write(const char *data, size_t length) {
    if (tty_context == 0) {
        return;
    }

    for (size_t i = 0; i < length; ++i) {
        if (data[i] == '\n') {
            flanterm_write(tty_context, "\r\n", 2);
        } else {
            flanterm_write(tty_context, &data[i], 1);
        }
    }
}

void xbks_tty_write_string(const char *text) {
    if (tty_context == 0) {
        return;
    }

    const char *cursor = text;
    while (*cursor != '\0') {
        ++cursor;
    }

    flanterm_write(tty_context, text, (size_t)(cursor - text));
}
