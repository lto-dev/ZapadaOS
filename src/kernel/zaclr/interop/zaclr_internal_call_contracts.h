#ifndef ZACLR_INTERNAL_CALL_CONTRACTS_H
#define ZACLR_INTERNAL_CALL_CONTRACTS_H

#include <kernel/zaclr/exec/zaclr_eval_stack.h>
#include <kernel/zaclr/exec/zaclr_frame.h>
#include <kernel/zaclr/include/zaclr_public_api.h>
#include <kernel/zaclr/metadata/zaclr_signature.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zaclr_native_bind_sig_flags {
    ZACLR_NATIVE_BIND_SIG_FLAG_NONE = 0x00,
    ZACLR_NATIVE_BIND_SIG_FLAG_BYREF = 0x01,
    ZACLR_NATIVE_BIND_SIG_FLAG_PINNED = 0x02
};

struct zaclr_native_call_frame {
    struct zaclr_runtime* runtime;
    struct zaclr_frame* caller_frame;
    const struct zaclr_loaded_assembly* assembly;
    const struct zaclr_method_desc* method;
    const struct zaclr_signature_desc* managed_signature;
    uint8_t invocation_kind;
    uint8_t has_this;
    uint8_t argument_count;
    uint8_t reserved0;
    struct zaclr_stack_value* this_value;
    struct zaclr_stack_value* arguments;
    struct zaclr_stack_value result_value;
    uint8_t has_result;
    uint8_t reserved1;
    uint16_t reserved2;
};

uint8_t zaclr_native_call_frame_argument_count(const struct zaclr_native_call_frame* frame);
uint8_t zaclr_native_call_frame_has_this(const struct zaclr_native_call_frame* frame);
uint8_t zaclr_native_call_frame_invocation_kind(const struct zaclr_native_call_frame* frame);

struct zaclr_stack_value* zaclr_native_call_frame_this(struct zaclr_native_call_frame* frame);
struct zaclr_stack_value* zaclr_native_call_frame_arg(struct zaclr_native_call_frame* frame,
                                                      uint32_t index);

struct zaclr_result zaclr_native_call_frame_arg_i4(struct zaclr_native_call_frame* frame,
                                                   uint32_t index,
                                                   int32_t* out_value);
struct zaclr_result zaclr_native_call_frame_arg_i8(struct zaclr_native_call_frame* frame,
                                                   uint32_t index,
                                                   int64_t* out_value);
struct zaclr_result zaclr_native_call_frame_arg_bool(struct zaclr_native_call_frame* frame,
                                                     uint32_t index,
                                                     bool* out_value);
struct zaclr_result zaclr_native_call_frame_arg_object(struct zaclr_native_call_frame* frame,
                                                       uint32_t index,
                                                       zaclr_object_handle* out_value);
struct zaclr_result zaclr_native_call_frame_arg_string(struct zaclr_native_call_frame* frame,
                                                       uint32_t index,
                                                       const struct zaclr_string_desc** out_value);
struct zaclr_result zaclr_native_call_frame_arg_array(struct zaclr_native_call_frame* frame,
                                                      uint32_t index,
                                                      const struct zaclr_array_desc** out_value);

struct zaclr_result zaclr_native_call_frame_load_byref_i4(struct zaclr_native_call_frame* frame,
                                                          uint32_t index,
                                                          int32_t* out_value);
struct zaclr_result zaclr_native_call_frame_load_byref_i8(struct zaclr_native_call_frame* frame,
                                                          uint32_t index,
                                                          int64_t* out_value);
struct zaclr_result zaclr_native_call_frame_store_byref_i4(struct zaclr_native_call_frame* frame,
                                                           uint32_t index,
                                                           int32_t value);
struct zaclr_result zaclr_native_call_frame_store_byref_i8(struct zaclr_native_call_frame* frame,
                                                           uint32_t index,
                                                           int64_t value);
struct zaclr_result zaclr_native_call_frame_load_byref_object(struct zaclr_native_call_frame* frame,
                                                              uint32_t index,
                                                              zaclr_object_handle* out_value);
struct zaclr_result zaclr_native_call_frame_store_byref_object(struct zaclr_native_call_frame* frame,
                                                               uint32_t index,
                                                               zaclr_object_handle value);

struct zaclr_result zaclr_native_call_frame_set_void(struct zaclr_native_call_frame* frame);
struct zaclr_result zaclr_native_call_frame_set_i4(struct zaclr_native_call_frame* frame,
                                                   int32_t value);
struct zaclr_result zaclr_native_call_frame_set_i8(struct zaclr_native_call_frame* frame,
                                                   int64_t value);
struct zaclr_result zaclr_native_call_frame_set_bool(struct zaclr_native_call_frame* frame,
                                                     bool value);
struct zaclr_result zaclr_native_call_frame_set_object(struct zaclr_native_call_frame* frame,
                                                       zaclr_object_handle value);
struct zaclr_result zaclr_native_call_frame_set_string(struct zaclr_native_call_frame* frame,
                                                       zaclr_object_handle value);

typedef struct zaclr_result (*zaclr_native_frame_handler)(
    struct zaclr_native_call_frame& frame);

struct zaclr_native_bind_sig_type {
    uint8_t element_type;
    uint8_t flags;
    uint16_t generic_index;
    const char* type_namespace;
    const char* type_name;
    const struct zaclr_native_bind_sig_type* child;
    const struct zaclr_native_bind_sig_type* generic_args;
    uint16_t generic_arg_count;
    uint16_t reserved;
};

struct zaclr_native_bind_signature {
    uint8_t has_this;
    uint8_t parameter_count;
    uint16_t reserved;
    struct zaclr_native_bind_sig_type return_type;
    const struct zaclr_native_bind_sig_type* parameter_types;
};

struct zaclr_native_bind_method {
    const char* type_namespace;
    const char* type_name;
    const char* method_name;
    struct zaclr_native_bind_signature signature;
    zaclr_native_frame_handler handler;
};

struct zaclr_internal_call_resolution {
    const char* assembly_name;
    const struct zaclr_native_bind_method* method;
};

bool zaclr_internal_call_text_equals(const char* left, const char* right);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_INTERNAL_CALL_CONTRACTS_H */
