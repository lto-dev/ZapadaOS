#include <kernel/arch/aarch64/uart.h>
#include <kernel/arch/aarch64/io.h>

/*
 * PL011 UART driver for AArch64.
 *
 * BOARD_UART_BASE selects the PL011 UART0 physical base address and is
 * injected at compile time by the Makefile via -DBOARD_UART_BASE=<addr>.
 *
 * Supported boards and their UART0 base addresses:
 *   BCM2837 (RPi 3, 3+, 3A+)  :  0x3F201000  [default, QEMU raspi3b]
 *   BCM2711 (RPi 4B, CM4)      :  0xFE201000
 *
 * Raspberry Pi 5 uses the RP1 south-bridge chip for peripheral I/O.
 * Its UART is not register-compatible at the same level as the BCM PL011
 * instances. RPi 5 support is deferred to a future driver stage.
 *
 * On x86_64 the COM1 port (0x3F8) is a PC-AT legacy standard present
 * identically on all x86 mainboards. AArch64 SoC vendors choose their
 * own MMIO addresses, which is why board selection is needed here.
 */

#ifndef BOARD_UART_BASE
#define BOARD_UART_BASE   0x3F201000UL   /* Default: BCM2837 (RPi 3, 3+, 3A+) */
#endif

/* PL011 register offsets (from BOARD_UART_BASE). */
#define UART_DR    0x00u  /* Data Register                  */
#define UART_FR    0x18u  /* Flag Register                  */
#define UART_IBRD  0x24u  /* Integer Baud Rate Divisor      */
#define UART_FBRD  0x28u  /* Fractional Baud Rate Divisor   */
#define UART_LCRH  0x2Cu  /* Line Control Register High     */
#define UART_CR    0x30u  /* Control Register               */
#define UART_IMSC  0x38u  /* Interrupt Mask Set/Clear       */
#define UART_ICR   0x44u  /* Interrupt Clear Register       */

/* FR register bits. */
#define FR_TXFF    (1u << 5)  /* Transmit FIFO Full    */
#define FR_RXFE    (1u << 4)  /* Receive FIFO Empty    */

/*
 * Baud rate configuration for 115200 at 3 MHz UART clock.
 *
 * The BCM2837 documentation specifies a 3 MHz UART reference clock
 * unless overridden by config.txt (init_uart_clock). QEMU raspi3b uses
 * the same 3 MHz default.
 *
 * BRD = UARTCLK / (16 * baud)
 *     = 3000000 / (16 * 115200)
 *     = 1.627...
 *
 * IBRD = floor(BRD) = 1
 * FBRD = round(frac * 64) = round(0.627 * 64) = round(40.128) = 40
 */
#define UART_IBRD_VAL   1u
#define UART_FBRD_VAL   40u

/* LCRH: WLEN=8 (bits [6:5]=0b11), FEN=1 (bit 4), no parity, 1 stop bit. */
#define UART_LCRH_8N1   0x70u

/* CR: UARTEN (bit 0), TXE (bit 8), RXE (bit 9). */
#define UART_CR_ENABLE  0x301u

static void uart_reg_write(unsigned int offset, uint32_t val)
{
    mmio_writel(BOARD_UART_BASE + offset, val);
}

static uint32_t uart_reg_read(unsigned int offset)
{
    return mmio_readl(BOARD_UART_BASE + offset);
}

void uart_init(void)
{
    /* Disable the UART before modifying any register. */
    uart_reg_write(UART_CR, 0);

    /* Mask all interrupts and clear any pending ones. */
    uart_reg_write(UART_IMSC, 0);
    uart_reg_write(UART_ICR, 0x7FFu);

    /*
     * Set the baud rate. IBRD must be written before LCRH because
     * writing LCRH causes the UART to internally latch the divisors.
     */
    uart_reg_write(UART_IBRD, UART_IBRD_VAL);
    uart_reg_write(UART_FBRD, UART_FBRD_VAL);

    /* 8-bit word length, FIFO enabled, 1 stop bit, no parity. */
    uart_reg_write(UART_LCRH, UART_LCRH_8N1);

    /* Enable the UART, TX path, and RX path. */
    uart_reg_write(UART_CR, UART_CR_ENABLE);
}

void uart_putc(char c)
{
    /* Spin until the TX FIFO has space for at least one character. */
    while (uart_reg_read(UART_FR) & FR_TXFF) {
        /* Busy-wait. */
    }
    uart_reg_write(UART_DR, (uint32_t)(unsigned char)c);
}

void uart_write(const char *str)
{
    while (*str != '\0') {
        /* Translate LF to CRLF for terminal emulators. */
        if (*str == '\n') {
            uart_putc('\r');
        }
        uart_putc(*str);
        str++;
    }
}

void uart_write_hex64(uint64_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;

    uart_putc('0');
    uart_putc('x');

    for (i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xFu]);
    }
}

void uart_write_dec(uint64_t val)
{
    char  buf[21];
    int   i;

    i       = 20;
    buf[20] = '\0';

    if (val == 0) {
        uart_putc('0');
        return;
    }

    while (val > 0u) {
        i--;
        buf[i] = (char)('0' + (int)(val % 10u));
        val   /= 10u;
    }

    uart_write(&buf[i]);
}
