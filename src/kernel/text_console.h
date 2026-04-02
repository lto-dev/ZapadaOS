/*
 * Zapada - src/kernel/text_console.h
 *
 * Minimal x86_64 text-mode console backed by the bootloader-provided text
 * framebuffer / VGA-compatible character buffer.
 */

#ifndef ZAPADA_TEXT_CONSOLE_H
#define ZAPADA_TEXT_CONSOLE_H

#include <kernel/types.h>

typedef struct {
    uintptr_t buffer_addr;
    uint32_t  cols;
    uint32_t  rows;
} text_console_info_t;

void text_console_init(const text_console_info_t *info);
int  text_console_is_ready(void);
void text_console_write(const char *str);
void text_console_write_hex64(uint64_t val);
void text_console_write_dec(uint64_t val);

#endif /* ZAPADA_TEXT_CONSOLE_H */


