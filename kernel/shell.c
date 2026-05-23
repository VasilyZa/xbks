#include <xbks/boot_info.h>
#include <xbks/io.h>
#include <xbks/keyboard.h>
#include <xbks/log.h>
#include <xbks/shell.h>
#include <xbks/string.h>

struct shell_command {
    const char *name;
    const char *summary;
    void (*handler)(const struct xbks_boot_info *boot_info);
};

static void command_help(const struct xbks_boot_info *boot_info);
static void command_info(const struct xbks_boot_info *boot_info);
static void command_memmap(const struct xbks_boot_info *boot_info);
static void command_clear(const struct xbks_boot_info *boot_info);
static void command_reboot(const struct xbks_boot_info *boot_info);

static const struct shell_command commands[] = {
    { "help", "list built-in commands", command_help },
    { "info", "print boot protocol state", command_info },
    { "memmap", "print memory map summary", command_memmap },
    { "clear", "clear the terminal", command_clear },
    { "reboot", "reset through the PS/2 controller", command_reboot },
};

static void write_prompt(void) {
    xbks_console_write_string("xbks> ");
}

static void command_help(const struct xbks_boot_info *boot_info) {
    (void)boot_info;

    xbks_console_write_string("built-in commands:\n");
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        xbks_console_write_string("  ");
        xbks_console_write_string(commands[i].name);
        xbks_console_write_string(" - ");
        xbks_console_write_string(commands[i].summary);
        xbks_console_write_string("\n");
    }
}

static void command_info(const struct xbks_boot_info *boot_info) {
    const char *bootloader_name = "(unknown)";
    const char *bootloader_version = "(unknown)";
    const char *cmdline = "";

    if (boot_info->bootloader != 0) {
        bootloader_name = boot_info->bootloader->name;
        bootloader_version = boot_info->bootloader->version;
    }

    if (boot_info->cmdline != 0 && boot_info->cmdline->cmdline != 0) {
        cmdline = boot_info->cmdline->cmdline;
    }

    xbks_log_printf(XBKS_LOG_INFO, "bootloader: %s %s", bootloader_name, bootloader_version);
    xbks_log_printf(XBKS_LOG_INFO, "cmdline: %s", cmdline);
}

static void command_memmap(const struct xbks_boot_info *boot_info) {
    if (boot_info->memmap == 0) {
        xbks_console_write_string("memory map unavailable\n");
        return;
    }

    xbks_log_printf(XBKS_LOG_INFO, "memory map has %llu entries", boot_info->memmap->entry_count);
}

static void command_clear(const struct xbks_boot_info *boot_info) {
    (void)boot_info;
    xbks_console_write_string("\x1b[2J\x1b[H");
}

static void command_reboot(const struct xbks_boot_info *boot_info) {
    (void)boot_info;
    xbks_console_write_string("rebooting...\n");

    while ((xbks_inb(0x64) & 0x02u) != 0) {
        __asm__ volatile ("pause");
    }

    xbks_outb(0x64, 0xfe);
    for (;;) {
        xbks_hlt();
    }
}

static void run_command(const char *name, const struct xbks_boot_info *boot_info) {
    if (name[0] == '\0') {
        return;
    }

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strcmp(name, commands[i].name) == 0) {
            commands[i].handler(boot_info);
            return;
        }
    }

    xbks_console_write_string("unknown command: ");
    xbks_console_write_string(name);
    xbks_console_write_string("\n");
}

void xbks_shell_run(const struct xbks_boot_info *boot_info) {
    char line[128];
    size_t line_length = 0;

    xbks_keyboard_init();
    xbks_console_write_string("\nxbks early shell\n");
    xbks_console_write_string("type 'help' for commands\n");
    write_prompt();

    for (;;) {
        const char ch = xbks_keyboard_read_char_blocking();

        if (ch == '\n') {
            xbks_console_write_string("\n");
            line[line_length] = '\0';
            run_command(line, boot_info);
            line_length = 0;
            write_prompt();
            continue;
        }

        if (ch == '\b') {
            if (line_length != 0) {
                --line_length;
                xbks_console_write_string("\b \b");
            }
            continue;
        }

        if (ch >= ' ' && ch <= '~' && line_length + 1 < sizeof(line)) {
            line[line_length] = ch;
            ++line_length;
            xbks_console_write(&ch, 1);
        }
    }
}
