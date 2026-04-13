#ifndef ZACLR_MARSHALLING_H
#define ZACLR_MARSHALLING_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zaclr_native_call_invocation_kind {
    ZACLR_NATIVE_CALL_INVOCATION_CALL = 1,
    ZACLR_NATIVE_CALL_INVOCATION_NEWOBJ = 2,
    ZACLR_NATIVE_CALL_INVOCATION_STATIC_FIELD = 3
};

struct zaclr_result zaclr_build_native_call_frame(struct zaclr_runtime* runtime,
                                                  struct zaclr_frame* caller_frame,
                                                  const struct zaclr_loaded_assembly* assembly,
                                                  const struct zaclr_method_desc* method,
                                                  uint8_t invocation_kind,
                                                  uint8_t has_this,
                                                  struct zaclr_stack_value* this_value,
                                                  uint8_t argument_count,
                                                  struct zaclr_stack_value* arguments,
                                                  struct zaclr_native_call_frame* frame);

struct zaclr_stack_value* zaclr_native_call_frame_resolve_byref_target(struct zaclr_native_call_frame* frame,
                                                                       uint32_t index);

struct zaclr_result zaclr_invoke_internal_call(struct zaclr_native_call_frame* frame,
                                               const struct zaclr_native_bind_method* method);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_MARSHALLING_H */
