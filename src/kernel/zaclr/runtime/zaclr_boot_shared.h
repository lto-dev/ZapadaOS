#ifndef ZACLR_BOOT_SHARED_H
#define ZACLR_BOOT_SHARED_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

void zaclr_boot_shared_set_command_line(const char* value);
const char* zaclr_boot_shared_get_command_line(void);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_BOOT_SHARED_H */
