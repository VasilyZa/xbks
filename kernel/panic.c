#include <xbks/io.h>
#include <xbks/log.h>
#include <xbks/panic.h>

XBKS_NORETURN void xbks_panic(const char *message) {
    xbks_log_write(XBKS_LOG_ERROR, message);
    xbks_cli();

    for (;;) {
        xbks_hlt();
    }
}
