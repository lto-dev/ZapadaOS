/*
 * Zapada - src/kernel/console.h
 *
 * Architecture-neutral console interface.
 *
 * This header is the single diagnostic output contract for all shared kernel
 * subsystems (PMM, heap, panic, kernel_start). Callers must never include an
 * architecture-specific output header (serial.h, uart.h) directly.
 *
 * Each architecture provides its own implementation:
 *   src/kernel/arch/x86_64/console.c  - wraps serial_write / serial_write_hex64
 *   src/kernel/arch/aarch64/console.c - wraps uart_write  / uart_write_hex64
 *
 * The console must be initialized by the architecture-specific entry point
 * (kernel_main / kernel_main_aarch64) before any shared subsystem is called.
 * console_init() calls the appropriate arch init (serial_init / uart_init).
 *
 * console_write         - print a null-terminated string
 * console_write_hex64   - print a 64-bit value as "0x<16 hex digits>"
 * console_write_dec     - print a 64-bit value as unsigned decimal
 * console_can_read      - report whether an input byte is ready
 * console_try_read_char - return an input byte or -1 when none is ready
 * console_read_char     - block until an input byte is available
 */

#ifndef ZAPADA_CONSOLE_H
#define ZAPADA_CONSOLE_H

#include <kernel/types.h>

void console_write(const char *str);
void console_write_hex64(uint64_t val);
void console_write_dec(uint64_t val);
bool console_can_read(void);
int console_try_read_char(void);
int console_read_char(void);

#endif /* ZAPADA_CONSOLE_H */


