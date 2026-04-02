/*
 * Zapada - src/kernel/fb_console.h
 *
 * Minimal framebuffer text console used as a secondary output path next to
 * serial/UART. The shared kernel continues to use console_write(); the arch
 * console adapter may mirror output to this framebuffer after initialization.
 */

#ifndef ZAPADA_FB_CONSOLE_H
#define ZAPADA_FB_CONSOLE_H

#include <kernel/types.h>

typedef struct {
    uintptr_t fb_addr;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;
    uint8_t   bpp;
    uint8_t   red_pos;
    uint8_t   red_mask_size;
    uint8_t   green_pos;
    uint8_t   green_mask_size;
    uint8_t   blue_pos;
    uint8_t   blue_mask_size;
} fb_console_info_t;

void fb_console_init(const fb_console_info_t *info);
int  fb_console_is_ready(void);
void fb_console_write(const char *str);
void fb_console_write_hex64(uint64_t val);
void fb_console_write_dec(uint64_t val);

#endif /* ZAPADA_FB_CONSOLE_H */


