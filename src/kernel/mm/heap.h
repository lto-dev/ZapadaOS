#ifndef ZAPADA_MM_HEAP_H
#define ZAPADA_MM_HEAP_H

#include <kernel/types.h>

/*
 * kheap_init - initialize the early bump heap.
 *
 * The heap starts at align_up(kernel_end, 4096) and spans EARLY_HEAP_SIZE
 * bytes (defined in mm_defs.h). This range is reserved from the PMM so the
 * two subsystems do not overlap.
 *
 * Must be called after gdt_init and idt_init but before any kheap_alloc call.
 */
void   kheap_init(void);
void   kheap_set_size(size_t heap_size);
void   kheap_reserve_until(uintptr_t min_start);

/*
 * kheap_alloc - allocate size bytes from the bump heap.
 *
 * Returns a pointer aligned to 16 bytes, or NULL if the heap is exhausted
 * or size is zero.
 */
void  *kheap_alloc(size_t size);

/*
 * kheap_free - return a previously allocated block to the heap.
 *
 * The block is marked free and coalesced with any adjacent free blocks.
 * If the freed block lies at the top of used memory the bump pointer is
 * rewound so the space can be reused without a scan.
 *
 * Passing NULL is safe and has no effect.
 */
void   kheap_free(void *ptr);

/*
 * kheap_get_free_bytes - return the number of bytes remaining in the heap.
 */
size_t kheap_get_free_bytes(void);

#endif /* ZAPADA_MM_HEAP_H */

