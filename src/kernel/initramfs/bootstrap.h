/*
 * Zapada - src/kernel/initramfs/bootstrap.h
 *
 * Early initramfs bootstrap helpers.
 */

#ifndef KERNEL_INITRAMFS_BOOTSTRAP_H
#define KERNEL_INITRAMFS_BOOTSTRAP_H

#include <kernel/types.h>

uint32_t initramfs_required_heap_bytes(const uint8_t *module_start, uint32_t module_size);
void initramfs_bootstrap(const uint8_t *module_start, uint32_t module_size);

#endif /* KERNEL_INITRAMFS_BOOTSTRAP_H */
