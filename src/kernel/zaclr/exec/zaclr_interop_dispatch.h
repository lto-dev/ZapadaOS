#ifndef ZACLR_INTEROP_DISPATCH_H
#define ZACLR_INTEROP_DISPATCH_H

#include <kernel/zaclr/include/zaclr_status.h>
#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>
#include <kernel/zaclr/loader/zaclr_loader.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_dispatch_context;
struct zaclr_frame;
struct zaclr_loaded_assembly;
struct zaclr_type_desc;
struct zaclr_method_desc;

struct zaclr_result zaclr_invoke_internal_call_exact(struct zaclr_dispatch_context* context,
                                                     struct zaclr_frame* frame,
                                                     const struct zaclr_loaded_assembly* owning_assembly,
                                                     const struct zaclr_type_desc* owning_type,
                                                     const struct zaclr_method_desc* method,
                                                     uint8_t invocation_kind);
struct zaclr_result zaclr_invoke_native_frame_handler_exact(struct zaclr_dispatch_context* context,
                                                            struct zaclr_frame* frame,
                                                            const struct zaclr_loaded_assembly* owning_assembly,
                                                            const struct zaclr_method_desc* method,
                                                            zaclr_native_frame_handler handler,
                                                            uint8_t invocation_kind);

struct zaclr_result zaclr_allocate_reference_type_instance(struct zaclr_runtime* runtime,
                                                           const struct zaclr_loaded_assembly* owning_assembly,
                                                           struct zaclr_token type_token,
                                                           struct zaclr_object_desc** out_object);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_INTEROP_DISPATCH_H */
