#ifndef ZACLR_HOST_H
#define ZACLR_HOST_H

#include <kernel/zaclr/include/zaclr_contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_host_vtable {
    void (*write_text)(const char* text);
    void (*write_hex64)(uint64_t value);
    void (*panic)(const char* file, int line, const char* message);
    uint64_t (*monotonic_ticks)(void);
    void* (*alloc_pages)(size_t size, uint32_t flags);
    void (*free_pages)(void* ptr, size_t size);
};

const struct zaclr_host_vtable* zaclr_host_kernel_vtable(void);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_HOST_H */
