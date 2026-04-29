#ifndef ZAPADA_ARCH_X86_64_PAGING_H
#define ZAPADA_ARCH_X86_64_PAGING_H

#include <kernel/types.h>

/*
 * x86_paging_identity_map_mmio_2m - add identity mappings for an MMIO range.
 *
 * The current x86_64 boot path starts with 2 MiB identity pages for the first
 * 1 GiB. PCI BARs can live above that range, so managed-driver HAL MMIO probes
 * need a small native mapper before volatile register access is safe.
 *
 * This helper maps the requested physical range using 2 MiB pages. It is a
 * Stage-3 bootstrap helper, not the final VM subsystem.
 */
int x86_paging_identity_map_mmio_2m(uint64_t physical_base, uint64_t length);

#endif /* ZAPADA_ARCH_X86_64_PAGING_H */
