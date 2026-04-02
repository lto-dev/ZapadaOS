#ifndef ZAPADA_ARCH_AARCH64_UART_H
#define ZAPADA_ARCH_AARCH64_UART_H

#include <kernel/types.h>

/*
 * PL011 UART driver for AArch64.
 *
 * The UART base address is selected at compile time via BOARD_UART_BASE,
 * injected by the Makefile -DBOARD_UART_BASE=<addr> flag. Build with:
 *   make ARCH=aarch64              (default: QEMU virt)
 *   make ARCH=aarch64 BOARD=virt   (QEMU virt, PL011 @ 0x09000000)
 *   make ARCH=aarch64 BOARD=rpi4  (RPi 4B / CM4, BCM2711)
 *
 * PL011 UART0 base addresses:
 *   QEMU virt                  : 0x09000000  [default]
 *   BCM2837 (RPi 3, 3+, 3A+)  : 0x3F201000
 *   BCM2711 (RPi 4B, CM4)     : 0xFE201000
 *
 * Raspberry Pi 5 uses the RP1 south-bridge chip for peripheral I/O.
 * The RP1 UART is not register-compatible at the same level. RPi 5 support
 * requires a separate driver stage and is not yet implemented.
 *
 * uart_init        - initialize the PL011 (8N1, 115200, FIFOs enabled).
 * uart_putc        - transmit a single character; blocks if TX FIFO full.
 * uart_write       - transmit a null-terminated string; LF -> CRLF.
 * uart_write_hex64 - transmit a 64-bit value as "0x<16 hex digits>".
 * uart_write_dec   - transmit a 64-bit value as unsigned decimal.
 */

void   uart_init(void);
void   uart_putc(char c);
void   uart_write(const char *str);
void   uart_write_hex64(uint64_t val);
void   uart_write_dec(uint64_t val);

#endif /* ZAPADA_ARCH_AARCH64_UART_H */

