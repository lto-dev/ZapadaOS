/*
 * Zapada - src/kernel/arch/x86_64/console.c
 *
 * x86_64 implementation of the arch-neutral console interface.
 *
 * Delegates to the COM1 serial driver. The serial port must be initialized
 * by kernel_main before any console function is called.
 */

#include <kernel/console.h>
#include <kernel/fb_console.h>
#include <kernel/serial.h>
#include <kernel/text_console.h>

void console_write(const char *str)
{
    serial_write(str);
    if (fb_console_is_ready()) {
        fb_console_write(str);
    }
    if (text_console_is_ready()) {
        text_console_write(str);
    }
}

void console_write_hex64(uint64_t val)
{
    serial_write_hex64(val);
    if (fb_console_is_ready()) {
        fb_console_write_hex64(val);
    }
    if (text_console_is_ready()) {
        text_console_write_hex64(val);
    }
}

void console_write_dec(uint64_t val)
{
    serial_write_dec(val);
    if (fb_console_is_ready()) {
        fb_console_write_dec(val);
    }
    if (text_console_is_ready()) {
        text_console_write_dec(val);
    }
}

bool console_can_read(void)
{
    return serial_can_read();
}

int console_try_read_char(void)
{
    return serial_try_read_char();
}

int console_read_char(void)
{
    return serial_read_char();
}

