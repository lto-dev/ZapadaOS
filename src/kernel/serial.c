/*
 * Zapada - src/kernel/serial.c
 *
 * Serial console driver for COM1 (I/O port base 0x3F8), 115200 baud, 8N1.
 *
 * This is the only source of output during Stage 1 bring-up. All diagnostic
 * messages, log lines, and panic output pass through this driver.
 *
 * Port I/O is performed through src/kernel/arch/x86_64/io.h, which is the
 * designated unsafe boundary for all direct hardware access.
 */

#include <kernel/arch/x86_64/io.h>
#include <kernel/serial.h>
#include <kernel/types.h>

/* COM1 base I/O port address */
#define COM1_PORT  0x3F8u

/* UART register offsets from the base port */
#define UART_DATA          0   /* Data register (read=RX, write=TX) */
#define UART_INT_ENABLE    1   /* Interrupt enable register */
#define UART_BAUD_LOW      0   /* Baud rate divisor, low byte  (DLAB=1) */
#define UART_BAUD_HIGH     1   /* Baud rate divisor, high byte (DLAB=1) */
#define UART_FIFO_CTRL     2   /* FIFO control register */
#define UART_LINE_CTRL     3   /* Line control register */
#define UART_MODEM_CTRL    4   /* Modem control register */
#define UART_LINE_STATUS   5   /* Line status register */
#define UART_MODEM_STATUS  6   /* Modem status register */
#define UART_SCRATCH       7   /* Scratch register */

/* Line status register bits */
#define LSR_TX_HOLDING_EMPTY  (1 << 5)   /* Transmitter holding register empty */
#define LSR_DATA_READY        (1 << 0)   /* Data available to read */

/* Modem control register bits */
#define MCR_LOOPBACK  (1 << 4)

/* Line control register: DLAB (Divisor Latch Access Bit) */
#define LCR_DLAB  (1 << 7)

/* Divisor for 115200 baud at 1.8432 MHz oscillator (standard UART clock) */
#define BAUD_DIVISOR_115200  1

/* --------------------------------------------------------------------------
 * serial_init
 * -------------------------------------------------------------------------- */
bool serial_init(void)
{
    /* Disable all UART interrupts (we use polling). */
    outb(COM1_PORT + UART_INT_ENABLE, 0x00);

    /* Enable DLAB to set the baud rate divisor. */
    outb(COM1_PORT + UART_LINE_CTRL, LCR_DLAB);

    /* Set 115200 baud divisor. */
    outb(COM1_PORT + UART_BAUD_LOW,  (uint8_t)(BAUD_DIVISOR_115200 & 0xFF));
    outb(COM1_PORT + UART_BAUD_HIGH, (uint8_t)((BAUD_DIVISOR_115200 >> 8) & 0xFF));

    /* 8 data bits, no parity, 1 stop bit (8N1). DLAB cleared. */
    outb(COM1_PORT + UART_LINE_CTRL, 0x03);

    /* Enable and clear FIFO, 14-byte threshold. */
    outb(COM1_PORT + UART_FIFO_CTRL, 0xC7);

    /* Set RTS, DSR, OUT1, OUT2 active. */
    outb(COM1_PORT + UART_MODEM_CTRL, 0x0B);

    /*
     * Self-test: enable loopback mode, send a test byte, verify it echoes.
     * This detects absent or faulty UART hardware in the emulator.
     */
    outb(COM1_PORT + UART_MODEM_CTRL, MCR_LOOPBACK | 0x0F);
    outb(COM1_PORT + UART_DATA, 0xAE);

    if (inb(COM1_PORT + UART_DATA) != 0xAE) {
        /* Loopback did not echo - serial hardware not available. */
        return false;
    }

    /* Disable loopback, restore normal operation. */
    outb(COM1_PORT + UART_MODEM_CTRL, 0x0F);

    return true;
}

/* --------------------------------------------------------------------------
 * serial_write_char
 * -------------------------------------------------------------------------- */
void serial_write_char(char c)
{
    /* Spin until the transmitter holding register is empty. */
    while ((inb(COM1_PORT + UART_LINE_STATUS) & LSR_TX_HOLDING_EMPTY) == 0) {
        /* busy wait */
    }

    outb(COM1_PORT + UART_DATA, (uint8_t)c);
}

/* --------------------------------------------------------------------------
 * serial_can_read
 * -------------------------------------------------------------------------- */
bool serial_can_read(void)
{
    return (inb(COM1_PORT + UART_LINE_STATUS) & LSR_DATA_READY) != 0;
}

/* --------------------------------------------------------------------------
 * serial_try_read_char
 * -------------------------------------------------------------------------- */
int serial_try_read_char(void)
{
    if (!serial_can_read()) {
        return -1;
    }

    return (int)(uint8_t)inb(COM1_PORT + UART_DATA);
}

/* --------------------------------------------------------------------------
 * serial_read_char
 * -------------------------------------------------------------------------- */
int serial_read_char(void)
{
    while (!serial_can_read()) {
        /* busy wait */
    }

    return (int)(uint8_t)inb(COM1_PORT + UART_DATA);
}

/* --------------------------------------------------------------------------
 * serial_write
 * -------------------------------------------------------------------------- */
void serial_write(const char *str)
{
    if (str == NULL) {
        return;
    }

    for (; *str != '\0'; str++) {
        if (*str == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(*str);
    }
}

/* --------------------------------------------------------------------------
 * serial_write_hex64
 * -------------------------------------------------------------------------- */
void serial_write_hex64(uint64_t value)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    char buf[19]; /* "0x" + 16 hex chars + null */
    int  i;

    buf[0]  = '0';
    buf[1]  = 'x';
    buf[18] = '\0';

    for (i = 17; i >= 2; i--) {
        buf[i] = hex_digits[value & 0xF];
        value >>= 4;
    }

    serial_write(buf);
}

/* --------------------------------------------------------------------------
 * serial_write_dec
 * -------------------------------------------------------------------------- */
void serial_write_dec(uint64_t value)
{
    char    buf[21]; /* max 20 decimal digits for uint64_t + null */
    int     i = 20;

    buf[20] = '\0';

    if (value == 0) {
        serial_write_char('0');
        return;
    }

    while (value > 0 && i > 0) {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    }

    serial_write(&buf[i]);
}

