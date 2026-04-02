/*
 * Zapada - src/kernel/fb_console.c
 *
 * Very small pixel-addressed framebuffer console.
 *
 * Constraints:
 * - No heap allocation
 * - No external font assets
 * - Good enough for bring-up diagnostics, not a final terminal
 *
 * Rendering strategy:
 * - 8x16 cell grid
 * - Solid block glyphs per character cell for now
 * - Distinct background clear + cursor advancement gives immediate visual
 *   feedback without introducing a large bitmap font table in this step
 */

#include <kernel/fb_console.h>
#include <kernel/types.h>

#define FB_CELL_W 8u
#define FB_CELL_H 16u

static fb_console_info_t s_fb;
static uint32_t s_cols;
static uint32_t s_rows;
static uint32_t s_cursor_x;
static uint32_t s_cursor_y;
static int      s_ready;

static uint32_t fb_make_color(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t color;
    color  = ((uint32_t)(r >> (8u - s_fb.red_mask_size))   << s_fb.red_pos);
    color |= ((uint32_t)(g >> (8u - s_fb.green_mask_size)) << s_fb.green_pos);
    color |= ((uint32_t)(b >> (8u - s_fb.blue_mask_size))  << s_fb.blue_pos);
    return color;
}

static void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    volatile uint32_t *p;
    uintptr_t addr;

    if (!s_ready || x >= s_fb.width || y >= s_fb.height || s_fb.bpp != 32u) {
        return;
    }

    addr = s_fb.fb_addr + (uintptr_t)(y * s_fb.pitch) + (uintptr_t)(x * 4u);
    p = (volatile uint32_t *)addr;
    *p = color;
}

static void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    uint32_t yy;
    uint32_t xx;

    for (yy = 0u; yy < h; yy++) {
        for (xx = 0u; xx < w; xx++) {
            fb_put_pixel(x + xx, y + yy, color);
        }
    }
}

static void fb_scroll_if_needed(void)
{
    uint32_t bg;

    if (s_cursor_y < s_rows) {
        return;
    }

    /* Simple bring-up behavior: clear screen instead of memory-moving scroll. */
    bg = fb_make_color(0u, 0u, 0u);
    fb_fill_rect(0u, 0u, s_fb.width, s_fb.height, bg);
    s_cursor_x = 0u;
    s_cursor_y = 0u;
}

static void fb_newline(void)
{
    s_cursor_x = 0u;
    s_cursor_y++;
    fb_scroll_if_needed();
}

static void fb_put_cell(char c)
{
    uint32_t x;
    uint32_t y;
    uint32_t fg;
    uint32_t bg;

    if (!s_ready) {
        return;
    }

    if (c == '\n') {
        fb_newline();
        return;
    }
    if (c == '\r') {
        s_cursor_x = 0u;
        return;
    }

    if (s_cursor_x >= s_cols) {
        fb_newline();
    }

    x = s_cursor_x * FB_CELL_W;
    y = s_cursor_y * FB_CELL_H;
    bg = fb_make_color(0u, 0u, 0u);
    fg = fb_make_color(0xD0u, 0xD0u, 0xD0u);

    /* Cell background. */
    fb_fill_rect(x, y, FB_CELL_W, FB_CELL_H, bg);

    /* Minimal visible glyph: draw a character block with a border.
     * This is intentionally tiny and dependency-free for bring-up. */
    if (c != ' ') {
        fb_fill_rect(x + 1u, y + 2u, FB_CELL_W - 2u, FB_CELL_H - 4u, fg);
        fb_fill_rect(x + 2u, y + 3u, FB_CELL_W - 4u, FB_CELL_H - 6u, bg);
    }

    s_cursor_x++;
}

void fb_console_init(const fb_console_info_t *info)
{
    uint32_t bg;

    if (info == (const fb_console_info_t *)0 || info->fb_addr == 0u ||
        info->width == 0u || info->height == 0u || info->pitch == 0u ||
        info->bpp != 32u) {
        s_ready = 0;
        return;
    }

    s_fb = *info;
    s_cols = s_fb.width / FB_CELL_W;
    s_rows = s_fb.height / FB_CELL_H;
    s_cursor_x = 0u;
    s_cursor_y = 0u;
    s_ready = (s_cols > 0u && s_rows > 0u) ? 1 : 0;
    if (!s_ready) {
        return;
    }

    bg = fb_make_color(0u, 0u, 0u);
    fb_fill_rect(0u, 0u, s_fb.width, s_fb.height, bg);
}

int fb_console_is_ready(void)
{
    return s_ready;
}

void fb_console_write(const char *str)
{
    if (!s_ready || str == (const char *)0) {
        return;
    }

    while (*str != '\0') {
        fb_put_cell(*str);
        str++;
    }
}

void fb_console_write_hex64(uint64_t val)
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
    fb_console_write(buf);
}

void fb_console_write_dec(uint64_t val)
{
    char buf[21];
    int  i;

    i = 20;
    buf[20] = '\0';

    if (val == 0u) {
        fb_console_write("0");
        return;
    }

    while (val > 0u) {
        i--;
        buf[i] = (char)('0' + (val % 10u));
        val /= 10u;
    }

    fb_console_write(&buf[i]);
}

