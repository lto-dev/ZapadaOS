#ifndef ZACLR_ASSEMBLY_SOURCE_INITRAMFS_H
#define ZACLR_ASSEMBLY_SOURCE_INITRAMFS_H

#include <kernel/zaclr/loader/zaclr_assembly_source.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct zaclr_assembly_source* zaclr_assembly_source_initramfs(void);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_ASSEMBLY_SOURCE_INITRAMFS_H */
