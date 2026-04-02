#ifndef ZACLR_BOOT_SHARED_H
#define ZACLR_BOOT_SHARED_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

void zaclr_boot_shared_set_boot_part_lba(int32_t value);
int32_t zaclr_boot_shared_get_boot_part_lba(void);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_BOOT_SHARED_H */
