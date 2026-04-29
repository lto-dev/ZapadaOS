/*
 * Zapada - src/kernel/arch/x86_64/paging.c
 *
 * Minimal runtime page-table extension helpers for the current lower-half,
 * identity-mapped x86_64 kernel. This is intentionally narrow: it only maps
 * MMIO BAR ranges as 2 MiB identity pages for early managed-driver smoke tests.
 */

#include <kernel/arch/x86_64/paging.h>
#include <kernel/mm/pmm.h>
#include <kernel/support/kernel_memory.h>

#define X86_PAGE_PRESENT       0x001ull
#define X86_PAGE_WRITE         0x002ull
#define X86_PAGE_PWT           0x008ull
#define X86_PAGE_PCD           0x010ull
#define X86_PAGE_HUGE          0x080ull
#define X86_PAGE_ADDR_MASK     0x000FFFFFFFFFF000ull
#define X86_PAGE_2M_SIZE       0x200000ull
#define X86_PAGE_4K_SIZE       0x1000ull
#define X86_PAGE_TABLE_ENTRIES 512u

static uint64_t* current_pml4(void)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return (uint64_t*)(uintptr_t)(cr3 & X86_PAGE_ADDR_MASK);
}

static uint64_t* alloc_table_page(void)
{
    void* page = pmm_alloc_frame();
    if (page == (void*)0) {
        return (uint64_t*)0;
    }

    kernel_memset(page, 0, (uint32_t)X86_PAGE_4K_SIZE);
    return (uint64_t*)page;
}

static uint64_t* ensure_next_table(uint64_t* table, uint32_t index, uint64_t flags)
{
    uint64_t entry;
    uint64_t* next;

    if (table == (uint64_t*)0 || index >= X86_PAGE_TABLE_ENTRIES) {
        return (uint64_t*)0;
    }

    entry = table[index];
    if ((entry & X86_PAGE_PRESENT) != 0u) {
        return (uint64_t*)(uintptr_t)(entry & X86_PAGE_ADDR_MASK);
    }

    next = alloc_table_page();
    if (next == (uint64_t*)0) {
        return (uint64_t*)0;
    }

    table[index] = ((uint64_t)(uintptr_t)next & X86_PAGE_ADDR_MASK) | flags;
    return next;
}

static void flush_tlb(void)
{
    uint64_t cr3;
    __asm__ volatile (
        "mov %%cr3, %0\n\t"
        "mov %0, %%cr3\n\t"
        : "=r"(cr3)
        :
        : "memory");
}

int x86_paging_identity_map_mmio_2m(uint64_t physical_base, uint64_t length)
{
    uint64_t start;
    uint64_t end;
    uint64_t address;
    uint64_t* pml4;
    uint64_t table_flags;
    uint64_t leaf_flags;

    if (physical_base == 0u || length == 0u) {
        return -1;
    }

    start = physical_base & ~(X86_PAGE_2M_SIZE - 1u);
    end = (physical_base + length + X86_PAGE_2M_SIZE - 1u) & ~(X86_PAGE_2M_SIZE - 1u);
    if (end <= start) {
        return -1;
    }

    pml4 = current_pml4();
    table_flags = X86_PAGE_PRESENT | X86_PAGE_WRITE;
    leaf_flags = X86_PAGE_PRESENT | X86_PAGE_WRITE | X86_PAGE_PCD | X86_PAGE_PWT | X86_PAGE_HUGE;

    for (address = start; address < end; address += X86_PAGE_2M_SIZE) {
        uint32_t pml4_index = (uint32_t)((address >> 39u) & 0x1FFu);
        uint32_t pdpt_index = (uint32_t)((address >> 30u) & 0x1FFu);
        uint32_t pd_index = (uint32_t)((address >> 21u) & 0x1FFu);
        uint64_t* pdpt = ensure_next_table(pml4, pml4_index, table_flags);
        uint64_t* pd;

        if (pdpt == (uint64_t*)0) {
            return -1;
        }

        pd = ensure_next_table(pdpt, pdpt_index, table_flags);
        if (pd == (uint64_t*)0) {
            return -1;
        }

        pd[pd_index] = (address & 0x000FFFFFFFE00000ull) | leaf_flags;
    }

    flush_tlb();
    return 0;
}
