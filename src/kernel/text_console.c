/*
 * Zapada - src/kernel/text_console.c
 *
 * Minimal text-mode console for x86_64 boot-time output.
 */

#include <kernel/text_console.h>
#include <kernel/types.h>

static volatile uint16_t *s_buf;
static uint32_t s_cols;
static uint32_t s_rows;
static uint32_t s_x;
static uint32_t s_y;
static uint8_t  s_attr;
static int      s_ready;

static void text_console_clear(void)
{
    uint32_t y;
    uint32_t x;
    uint16_t blank;

    if (!s_ready) {
        return;
    }

    blank = (uint16_t)(((uint16_t)s_attr << 8) | (uint16_t)' ');
    for (y = 0u; y < s_rows; y++) {
        for (x = 0u; x < s_cols; x++) {
            s_buf[y * s_cols + x] = blank;
        }
    }

    s_x = 0u;
    s_y = 0u;
}

static void text_console_scroll(void)
{
    uint32_t y;
    uint32_t x;
    uint16_t blank;

    if (s_y < s_rows) {
        return;
    }

    for (y = 1u; y < s_rows; y++) {
        for (x = 0u; x < s_cols; x++) {
            s_buf[(y - 1u) * s_cols + x] = s_buf[y * s_cols + x];
        }
    }

    blank = (uint16_t)(((uint16_t)s_attr << 8) | (uint16_t)' ');
    for (x = 0u; x < s_cols; x++) {
        s_buf[(s_rows - 1u) * s_cols + x] = blank;
    }

    s_y = s_rows - 1u;
}

static void text_console_putc(char c)
{
    if (!s_ready) {
        return;
    }

    if (c == '\r') {
        s_x = 0u;
        return;
    }
    if (c == '\n') {
        s_x = 0u;
        s_y++;
        text_console_scroll();
        return;
    }

    if (s_x >= s_cols) {
        s_x = 0u;
        s_y++;
        text_console_scroll();
    }

    s_buf[s_y * s_cols + s_x] = (uint16_t)(((uint16_t)s_attr << 8) | (uint8_t)c);
    s_x++;
}

void text_console_init(const text_console_info_t *info)
{
    if (info == (const text_console_info_t *)0 || info->buffer_addr == 0u ||
        info->cols == 0u || info->rows == 0u) {
        s_ready = 0;
        return;
    }

    s_buf = (volatile uint16_t *)info->buffer_addr;
    s_cols = info->cols;
    s_rows = info->rows;
    s_attr = 0x07u;
    s_ready = 1;
    text_console_clear();
}

int text_console_is_ready(void)
{
    return s_ready;
}

void text_console_write(const char *str)
{
    if (!s_ready || str == (const char *)0) {
        return;
    }

    while (*str != '\0') {
        text_console_putc(*str);
        str++;
    }
}

void text_console_write_hex64(uint64_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[19];
    int i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; i++) {
        buf[2 + i] = hex[(val >> (60 - (i * 4))) & 0xFu];
    }
    buf[18] = '\0';
    text_console_write(buf);
}

void text_console_write_dec(uint64_t val)
{
    char buf[21];
    int  i;

    i = 20;
    buf[20] = '\0';

    if (val == 0u) {
        text_console_write("0");
        return;
    }

    while (val > 0u) {
        i--;
        buf[i] = (char)('0' + (val % 10u));
        val /= 10u;
    }

    text_console_write(&buf[i]);
}

