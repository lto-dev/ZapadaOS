/*
 * Zapada - src/kernel/serial.h
 *
 * Serial console interface (COM1, I/O port 0x3F8).
 *
 * This is the primary diagnostic output channel for Stage 1 bring-up.
 * All kernel log output and panic messages are routed through here.
 *
 * Unsafe boundary: port I/O instructions (inb/outb) are used internally.
 * Callers receive a clean string-based interface with no raw port access.
 */

#ifndef ZAPADA_SERIAL_H
#define ZAPADA_SERIAL_H

#include <kernel/types.h>

/*
 * serial_init - Initialize COM1 at 115200 baud, 8N1.
 *
 * Must be called once before any other serial function.
 * Returns true on success, false if the loopback self-test fails
 * (indicates no serial hardware or a faulty UART).
 */
bool serial_init(void);

/*
 * serial_write_char - Write a single character to COM1.
 *
 * Blocks until the transmitter holding register is empty.
 */
void serial_write_char(char c);

/*
 * serial_write - Write a null-terminated string to COM1.
 *
 * A '\n' in the string also emits a '\r' for terminal compatibility.
 */
void serial_write(const char *str);

/*
 * serial_write_hex64 - Write a 64-bit value as a 0x-prefixed hex string.
 *
 * Useful for printing addresses and register values in diagnostics.
 */
void serial_write_hex64(uint64_t value);

/*
 * serial_write_dec - Write an unsigned 64-bit value as a decimal string.
 */
void serial_write_dec(uint64_t value);

#endif /* ZAPADA_SERIAL_H */


