/*
 * Zapada - src/kernel/initramfs/loader.h
 *
 * Phase 3: pre-load managed DLL assemblies from the initramfs ramdisk
 * into the managed runtime assembly registry before managed boot code runs.
 *
 * This pre-loads the full managed boot set, including Zapada.Boot.dll, from
 * the initramfs before native code transfers control into managed execution.
 */

#ifndef KERNEL_INITRAMFS_LOADER_H
#define KERNEL_INITRAMFS_LOADER_H

#include <kernel/types.h>

/*
 * initramfs_load_drivers - scan the ramdisk for PE DLL files and register
 *                          each one in the managed runtime assembly table.
 *
 * Must be called after initramfs_bootstrap() has materialized the ramdisk
 * and before managed_runtime_run() executes the managed boot entry point.
 *
 * Returns the number of assemblies successfully loaded.
 */
uint32_t initramfs_load_drivers(void);

#endif /* KERNEL_INITRAMFS_LOADER_H */
