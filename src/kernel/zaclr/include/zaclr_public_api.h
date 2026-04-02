#ifndef ZACLR_PUBLIC_API_H
#define ZACLR_PUBLIC_API_H

#include <kernel/zaclr/include/zaclr_contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_launch_request {
    const char* image_path;
    const char* entry_type;
    const char* entry_method;
    zaclr_user_id user;
    zaclr_group_id group;
    struct zaclr_stdio_binding stdio;
    uint32_t flags;
};

struct zaclr_runtime_config {
    struct zaclr_trace_config trace;
    bool enable_metadata_validation;
    bool enable_opcode_trace;
    bool enable_heap_trace;
};

struct zaclr_runtime;

struct zaclr_result zaclr_runtime_initialize(struct zaclr_runtime* runtime,
                                             const struct zaclr_bootstrap_contract* bootstrap,
                                             const struct zaclr_runtime_config* config);
struct zaclr_result zaclr_runtime_shutdown(struct zaclr_runtime* runtime);
struct zaclr_result zaclr_runtime_launch(struct zaclr_runtime* runtime,
                                         const struct zaclr_launch_request* request,
                                         zaclr_process_id* out_process_id);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_PUBLIC_API_H */
