#ifndef ZACLR_BOOT_H
#define ZACLR_BOOT_H

#include <kernel/zaclr/include/zaclr_public_api.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_result zaclr_bootstrap_entry(struct zaclr_runtime* runtime,
                                          const struct zaclr_bootstrap_contract* bootstrap,
                                          const struct zaclr_runtime_config* config);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_BOOT_H */
