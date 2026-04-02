/*
 * Zapada - src/kernel/mm/pmm.c
 *
 * Stack-based physical frame allocator.
 *
 * Two initialization paths are provided:
 *
 *   pmm_init(mb_info)            - x86_64: walks Multiboot2 memory map.
 *   pmm_init_range(base, size)   - AArch64: flat single-region init (from FDT
 *                                  or hardcoded layout). No Multiboot2 needed.
 *
 * Both paths respect the address ownership contract (see mm_defs.h):
 *   [kernel_end_aligned, kernel_end_aligned + EARLY_HEAP_SIZE)  ->  heap
 *   [kernel_end_aligned + EARLY_HEAP_SIZE, ...)                 ->  PMM
 *
 * PMM_MAX_FREE_FRAMES caps the number of tracked frames at 32768 (128 MiB).
 * Systems with more available RAM will have frames above that limit silently
 * discarded. This is a known Stage 1 constraint.
 */

#include <kernel/console.h>
#include <kernel/mm/mm_defs.h>
#include <kernel/mm/pmm.h>

#define PMM_FRAME_SIZE       4096ull
#define PMM_MAX_FREE_FRAMES  32768u
#define PMM_MIN_USABLE_ADDR  0x00100000ull

static uint64_t g_free_frames[PMM_MAX_FREE_FRAMES];
static uint32_t g_free_frame_count;
static uint64_t g_reserved_until;

void pmm_reserve_until(uint64_t min_addr)
{
    if (min_addr > g_reserved_until) {
        g_reserved_until = min_addr;
    }
}

void pmm_init(mb2_info_t *mb_info)
{
    mb2_tag_mmap_t   *mmap_tag;
    mb2_mmap_entry_t *entry;
    uint32_t          entry_count;
    uint32_t          i;
    uint64_t          reserved_end;

    g_free_frame_count = 0;

    mmap_tag = (mb2_tag_mmap_t *)mb2_find_tag(mb_info, MB2_TAG_MEMORY_MAP);
    if (mmap_tag == NULL) {
        console_write("PMM init           : no memory map tag - allocator empty\n");
        return;
    }

    /*
     * reserved_end is the first PMM-usable physical address.
     * It accounts for the kernel image and the bump heap reservation so that
     * the PMM and heap do not overlap.
     */
    reserved_end = mm_align_up_u64((uint64_t)(uintptr_t)&kernel_end,
                                   PMM_FRAME_SIZE)
                   + (uint64_t)EARLY_HEAP_SIZE;
    if (g_reserved_until > reserved_end) {
        reserved_end = mm_align_up_u64(g_reserved_until, PMM_FRAME_SIZE);
    }

    entry_count = (mmap_tag->size - sizeof(mb2_tag_mmap_t)) / mmap_tag->entry_size;
    entry = (mb2_mmap_entry_t *)((uint8_t *)mmap_tag + sizeof(mb2_tag_mmap_t));

    for (i = 0; i < entry_count; i++) {
        if (entry->type == MB2_MEMORY_AVAILABLE) {
            uint64_t region_start;
            uint64_t region_end;
            uint64_t frame;

            region_start = mm_align_up_u64(entry->base_addr, PMM_FRAME_SIZE);
            region_end   = entry->base_addr + entry->length;

            /* Skip the BIOS low-memory area, kernel image, and heap reservation. */
            region_start = mm_max_u64(region_start, PMM_MIN_USABLE_ADDR);
            region_start = mm_max_u64(region_start, reserved_end);

            for (frame = region_start;
                 frame + PMM_FRAME_SIZE <= region_end
                     && g_free_frame_count < PMM_MAX_FREE_FRAMES;
                 frame += PMM_FRAME_SIZE) {
                g_free_frames[g_free_frame_count++] = frame;
            }
        }

        entry = (mb2_mmap_entry_t *)((uint8_t *)entry + mmap_tag->entry_size);
    }
}

void *pmm_alloc_frame(void)
{
    if (g_free_frame_count == 0) {
        return NULL;
    }

    g_free_frame_count--;
    return (void *)(uintptr_t)g_free_frames[g_free_frame_count];
}

void *pmm_alloc_contiguous(uint32_t count)
{
    uint32_t start;
    uint32_t i;

    if (count == 0u || count > g_free_frame_count) {
        return NULL;
    }

    for (start = 0u; start + count <= g_free_frame_count; start++) {
        uint64_t base = g_free_frames[start];
        int contiguous = 1;

        for (i = 1u; i < count; i++) {
            if (g_free_frames[start + i] != (base + ((uint64_t)i * PMM_FRAME_SIZE))) {
                contiguous = 0;
                break;
            }
        }

        if (contiguous != 0) {
            uint32_t tail;

            for (tail = start + count; tail < g_free_frame_count; tail++) {
                g_free_frames[tail - count] = g_free_frames[tail];
            }
            g_free_frame_count -= count;
            return (void *)(uintptr_t)base;
        }
    }

    return NULL;
}

void pmm_free_frame(void *frame)
{
    if (frame == NULL || g_free_frame_count >= PMM_MAX_FREE_FRAMES) {
        return;
    }

    g_free_frames[g_free_frame_count++] = (uint64_t)(uintptr_t)frame;
}

uint64_t pmm_get_free_frame_count(void)
{
    return g_free_frame_count;
}

/*
 * pmm_init_range - Initialize the PMM from a single flat physical region.
 *
 * Used on AArch64 where the memory layout is described by a single base+size
 * pair from the FDT or a hardcoded platform constant, rather than a
 * Multiboot2 memory map.
 *
 * The same address ownership contract applies as for pmm_init:
 *   [kernel_end_aligned, kernel_end_aligned + EARLY_HEAP_SIZE) -> heap
 *   [kernel_end_aligned + EARLY_HEAP_SIZE, ...)                -> PMM
 *
 * Frames are registered starting at max(mem_base, reserved_end) up to
 * mem_base + mem_size.
 */
void pmm_init_range(uint64_t mem_base, uint64_t mem_size)
{
    uint64_t reserved_end;
    uint64_t region_start;
    uint64_t region_end;
    uint64_t frame;

    g_free_frame_count = 0;

    if (mem_size == 0) {
        console_write("PMM init           : no memory region provided\n");
        return;
    }

    reserved_end = mm_align_up_u64((uint64_t)(uintptr_t)&kernel_end,
                                   PMM_FRAME_SIZE)
                   + (uint64_t)EARLY_HEAP_SIZE;
    if (g_reserved_until > reserved_end) {
        reserved_end = mm_align_up_u64(g_reserved_until, PMM_FRAME_SIZE);
    }

    region_start = (reserved_end > mem_base) ? reserved_end : mem_base;
    region_end   = mem_base + mem_size;

    region_start = mm_align_up_u64(region_start, PMM_FRAME_SIZE);

    for (frame = region_start;
         frame + PMM_FRAME_SIZE <= region_end
             && g_free_frame_count < PMM_MAX_FREE_FRAMES;
         frame += PMM_FRAME_SIZE) {
        g_free_frames[g_free_frame_count++] = frame;
    }
}

