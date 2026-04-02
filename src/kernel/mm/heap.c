/*
 * Zapada - src/kernel/mm/heap.c
 *
 * Early bump heap allocator.
 *
 * The heap occupies a fixed-size region immediately above the kernel image:
 *
 *   start = align_up(kernel_end, 4096)
 *   end   = start + EARLY_HEAP_SIZE
 *
 * This range is explicitly reserved from the PMM (see pmm.c and mm_defs.h),
 * so there is no physical address overlap between the two subsystems.
 *
 * The heap supports allocation only. There is no free path; this is by design
 * for an early bump allocator used before a general-purpose allocator exists.
 *
 * EARLY_HEAP_SIZE is defined in mm_defs.h and must match the reservation
 * applied in pmm.c.
 */

#include <kernel/mm/heap.h>
#include <kernel/mm/mm_defs.h>

#define KHEAP_PAGE_ALIGN  4096u
#define KHEAP_ALIGNMENT   16u

static uintptr_t g_heap_current;
static uintptr_t g_heap_end;
static uintptr_t g_heap_min_start;
static size_t    g_heap_size = EARLY_HEAP_SIZE;

void kheap_set_size(size_t heap_size)
{
    if (g_heap_current != 0u || heap_size == 0u) {
        return;
    }

    g_heap_size = heap_size;
}

void kheap_init(void)
{
    uintptr_t start;

    start          = mm_align_up_uintptr((uintptr_t)&kernel_end, KHEAP_PAGE_ALIGN);
    if (g_heap_min_start > start) {
        start = mm_align_up_uintptr(g_heap_min_start, KHEAP_PAGE_ALIGN);
    }
    g_heap_current = start;
    g_heap_end     = start + (uintptr_t)g_heap_size;
}

void kheap_reserve_until(uintptr_t min_start)
{
    uintptr_t aligned;

    if (g_heap_current == 0u) {
        if (min_start > g_heap_min_start) {
            g_heap_min_start = min_start;
        }
        return;
    }

    if (min_start <= g_heap_current) {
        return;
    }

    aligned = mm_align_up_uintptr(min_start, KHEAP_ALIGNMENT);
    if (aligned > g_heap_end) {
        g_heap_current = g_heap_end;
        return;
    }

    g_heap_current = aligned;
}

void *kheap_alloc(size_t size)
{
    uintptr_t start;
    uintptr_t end;

    if (size == 0 || g_heap_current == 0) {
        return NULL;
    }

    start = mm_align_up_uintptr(g_heap_current, KHEAP_ALIGNMENT);
    end   = start + (uintptr_t)size;

    if (end < start || end > g_heap_end) {
        return NULL;
    }

    g_heap_current = end;
    return (void *)start;
}

size_t kheap_get_free_bytes(void)
{
    if (g_heap_end <= g_heap_current) {
        return 0;
    }

    return (size_t)(g_heap_end - g_heap_current);
}

