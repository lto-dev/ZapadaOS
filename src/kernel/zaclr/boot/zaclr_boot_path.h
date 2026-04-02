#ifndef ZACLR_BOOT_PATH_H
#define ZACLR_BOOT_PATH_H

#include <kernel/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum zaclr_kernel_boot_status {
    ZACLR_KERNEL_BOOT_STATUS_OK = 0,
    ZACLR_KERNEL_BOOT_STATUS_BOOTSTRAP_FAILED = 1,
    ZACLR_KERNEL_BOOT_STATUS_LAUNCH_FAILED = 2,
} zaclr_kernel_boot_status_t;

zaclr_kernel_boot_status_t zaclr_boot_kernel_entry(void);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_BOOT_PATH_H */
