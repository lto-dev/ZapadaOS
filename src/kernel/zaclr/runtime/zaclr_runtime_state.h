#ifndef ZACLR_RUNTIME_STATE_H
#define ZACLR_RUNTIME_STATE_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_runtime_state {
    const struct zaclr_host_vtable* host;
    struct zaclr_runtime_config config;
    zaclr_process_id boot_process_id;
    zaclr_thread_id boot_thread_id;
    zaclr_app_domain_id boot_domain_id;
    zaclr_assembly_id boot_assembly_id;
    zaclr_method_id boot_entry_method_id;
    zaclr_method_id boot_completed_method_id;
    uint32_t flags;
};

#define ZACLR_RUNTIME_STATE_FLAG_BOOT_EXECUTED 0x00000001u
#define ZACLR_RUNTIME_STATE_FLAG_BOOT_COMPLETED 0x00000002u

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_RUNTIME_STATE_H */
