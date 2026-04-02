#ifndef ZAPADA_MM_PMM_H
#define ZAPADA_MM_PMM_H

#include <boot/multiboot2.h>
#include <kernel/types.h>

/*
 * pmm_init - Initialize from a Multiboot2 memory map (x86_64 path).
 *
 * Walks all available regions, skips the BIOS low area, the kernel image,
 * and the bump heap reservation.
 */
void     pmm_init(mb2_info_t *mb_info);
void     pmm_reserve_until(uint64_t min_addr);

/*
 * pmm_init_range - Initialize from a single flat physical region (AArch64 path).
 *
 * Registers frames within [mem_base, mem_base + mem_size) that are above the
 * kernel image and bump heap reservation. No Multiboot2 dependency.
 */
void     pmm_init_range(uint64_t mem_base, uint64_t mem_size);

void    *pmm_alloc_frame(void);
void    *pmm_alloc_contiguous(uint32_t count);
void     pmm_free_frame(void *frame);
uint64_t pmm_get_free_frame_count(void);

#endif /* ZAPADA_MM_PMM_H */

