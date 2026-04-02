/*
 * Zapada - src/kernel/mm/mm_defs.h
 *
 * Shared memory management constants, the kernel_end symbol, and common
 * utility helpers used by both the physical frame allocator (pmm) and the
 * early bump heap.
 *
 * Address ownership contract:
 *
 *   [kernel_end_aligned, kernel_end_aligned + EARLY_HEAP_SIZE)  ->  bump heap
 *   [kernel_end_aligned + EARLY_HEAP_SIZE, ...)                 ->  PMM frames
 *
 * The two subsystems do not overlap. The PMM will not register frames within
 * the heap reservation region.
 */

#ifndef ZAPADA_MM_DEFS_H
#define ZAPADA_MM_DEFS_H

#include <kernel/types.h>

/*
 * EARLY_HEAP_SIZE - size of the bump heap reserved immediately above the end
 * of the kernel image. 8 MiB is sufficient for all early kernel allocations
 * at Stage 1 and Stage 2.
 *
 * The PMM is not permitted to register frames within this range.
 */
#define EARLY_HEAP_SIZE  (8u * 1024u * 1024u)

/*
 * kernel_end - linker-defined symbol marking the byte immediately past the
 * end of the kernel BSS section. Take its address to get the end address.
 *
 * Do not read or write this as a variable - use (uintptr_t)&kernel_end.
 */
extern uint8_t kernel_end;

/*
 * mm_align_up_u64 - round a uint64_t value up to the next multiple of
 * alignment. alignment must be a power of two.
 */
static inline uint64_t mm_align_up_u64(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

/*
 * mm_align_up_uintptr - round a uintptr_t value up to the next multiple of
 * alignment. alignment must be a power of two.
 */
static inline uintptr_t mm_align_up_uintptr(uintptr_t value, uintptr_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

/*
 * mm_max_u64 - return the larger of two uint64_t values.
 */
static inline uint64_t mm_max_u64(uint64_t left, uint64_t right)
{
    return left > right ? left : right;
}

#endif /* ZAPADA_MM_DEFS_H */


