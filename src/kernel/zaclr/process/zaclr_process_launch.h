#ifndef ZACLR_PROCESS_LAUNCH_H
#define ZACLR_PROCESS_LAUNCH_H

#include <kernel/zaclr/loader/zaclr_assembly_registry.h>
#include <kernel/zaclr/process/zaclr_app_domain.h>
#include <kernel/zaclr/process/zaclr_handle_table.h>
#include <kernel/zaclr/process/zaclr_process.h>
#include <kernel/zaclr/process/zaclr_security_context.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_launch_state {
    struct zaclr_process process;
    struct zaclr_thread thread;
    struct zaclr_app_domain domain;
    struct zaclr_security_context security_context;
    struct zaclr_handle_table handle_table;
    struct zaclr_method_locator entry_point;
    const struct zaclr_loaded_assembly* assembly;
    const struct zaclr_method_desc* entry_method;
    const char* image_path;
    uint32_t flags;
};

struct zaclr_result zaclr_process_launch_request_validate(const struct zaclr_launch_request* request);
struct zaclr_result zaclr_process_resolve_launch_entry_point(const struct zaclr_loaded_assembly* assembly,
                                                             const struct zaclr_launch_request* request,
                                                             struct zaclr_method_locator* out_locator,
                                                             const struct zaclr_method_desc** out_method);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_PROCESS_LAUNCH_H */
