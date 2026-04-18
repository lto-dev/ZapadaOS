#ifndef ZAPADA_KERNEL_MEMORY_H
#define ZAPADA_KERNEL_MEMORY_H

#include <kernel/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void*  kernel_alloc(size_t size);
void   kernel_free(void* ptr);
void   kernel_memcpy(void* dst, const void* src, uint32_t n);
void   kernel_memset(void* dst, uint8_t val, uint32_t n);
size_t kernel_strlen(const char* text);
size_t kernel_get_free_bytes(void);

#ifdef __cplusplus
}
#endif

#endif /* ZAPADA_KERNEL_MEMORY_H */
