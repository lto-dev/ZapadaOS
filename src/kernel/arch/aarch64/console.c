/*
 * Zapada - src/kernel/arch/aarch64/console.c
 *
 * AArch64 implementation of the arch-neutral console interface.
 *
 * Delegates to the PL011 UART driver. The UART must be initialized
 * by kernel_main_aarch64 before any console function is called.
 */

#include <kernel/console.h>
#include <kernel/arch/aarch64/uart.h>

void console_write(const char *str)
{
    uart_write(str);
}

void console_write_hex64(uint64_t val)
{
    uart_write_hex64(val);
}

void console_write_dec(uint64_t val)
{
    uart_write_dec(val);
}

