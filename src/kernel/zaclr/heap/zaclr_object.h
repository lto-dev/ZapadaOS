#ifndef ZACLR_OBJECT_H
#define ZACLR_OBJECT_H

#include <kernel/zaclr/exec/zaclr_eval_stack.h>
#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_object_desc {
    zaclr_object_handle handle;
    zaclr_type_id type_id;
    const struct zaclr_loaded_assembly* owning_assembly;
    uint32_t size_bytes;
    uint32_t flags;
    uint16_t family;
    uint8_t gc_mark;
    uint8_t gc_state;
};

struct zaclr_boxed_value_desc {
    struct zaclr_object_desc object;
    uint32_t type_token_raw;
    uint32_t reserved;
    struct zaclr_stack_value value;
};

struct zaclr_reference_object_desc {
    struct zaclr_object_desc object;
    uint32_t type_token_raw;
    uint32_t field_capacity;
};

struct zaclr_runtime;
struct zaclr_loaded_assembly;

enum zaclr_object_family {
    ZACLR_OBJECT_FAMILY_UNKNOWN = 0,
    ZACLR_OBJECT_FAMILY_INSTANCE = 1,
    ZACLR_OBJECT_FAMILY_ARRAY = 2,
    ZACLR_OBJECT_FAMILY_STRING = 3
};

enum {
    ZACLR_OBJECT_FLAG_STRING = 0x00000001u,
    ZACLR_OBJECT_FLAG_BOXED_VALUE = 0x00000002u,
    ZACLR_OBJECT_FLAG_REFERENCE_TYPE = 0x00000004u,
    ZACLR_OBJECT_FLAG_CONTAINS_REFERENCES = 0x00000008u
};

enum {
    ZACLR_OBJECT_GC_STATE_NONE = 0u,
    ZACLR_OBJECT_GC_STATE_PINNED = 0x01u,
    ZACLR_OBJECT_GC_STATE_FINALIZER_PENDING = 0x02u,
    ZACLR_OBJECT_GC_STATE_FINALIZER_QUEUED = 0x04u,
    ZACLR_OBJECT_GC_STATE_FINALIZER_COMPLETE = 0x08u
};

uint32_t zaclr_object_flags(const struct zaclr_object_desc* object);
uint32_t zaclr_object_size_bytes(const struct zaclr_object_desc* object);
uint32_t zaclr_object_family(const struct zaclr_object_desc* object);
uint32_t zaclr_object_contains_references(const struct zaclr_object_desc* object);
uint32_t zaclr_object_is_marked(const struct zaclr_object_desc* object);
void zaclr_object_set_marked(struct zaclr_object_desc* object, uint32_t marked);
struct zaclr_result zaclr_boxed_value_allocate(struct zaclr_heap* heap,
                                               struct zaclr_token token,
                                               const struct zaclr_stack_value* value,
                                               zaclr_object_handle* out_handle);
struct zaclr_boxed_value_desc* zaclr_boxed_value_from_handle(struct zaclr_heap* heap,
                                                             zaclr_object_handle handle);
const struct zaclr_boxed_value_desc* zaclr_boxed_value_from_handle_const(const struct zaclr_heap* heap,
                                                                         zaclr_object_handle handle);
struct zaclr_result zaclr_reference_object_allocate(struct zaclr_heap* heap,
                                                    const struct zaclr_loaded_assembly* owning_assembly,
                                                    zaclr_type_id type_id,
                                                    struct zaclr_token type_token,
                                                    uint32_t field_capacity,
                                                    zaclr_object_handle* out_handle);
struct zaclr_reference_object_desc* zaclr_reference_object_from_handle(struct zaclr_heap* heap,
                                                                       zaclr_object_handle handle);
const struct zaclr_reference_object_desc* zaclr_reference_object_from_handle_const(const struct zaclr_heap* heap,
                                                                                   zaclr_object_handle handle);
struct zaclr_result zaclr_reference_object_store_field(struct zaclr_reference_object_desc* object,
                                                       struct zaclr_token token,
                                                       const struct zaclr_stack_value* value);
struct zaclr_result zaclr_reference_object_load_field(const struct zaclr_reference_object_desc* object,
                                                      struct zaclr_token token,
                                                      struct zaclr_stack_value* out_value);
struct zaclr_stack_value* zaclr_reference_object_field_storage(struct zaclr_reference_object_desc* object,
                                                              struct zaclr_token token);
const struct zaclr_stack_value* zaclr_reference_object_field_storage_const(const struct zaclr_reference_object_desc* object,
                                                                           struct zaclr_token token);
struct zaclr_result zaclr_object_store_field(struct zaclr_runtime* runtime,
                                             zaclr_object_handle handle,
                                             struct zaclr_token token,
                                             const struct zaclr_stack_value* value);
struct zaclr_result zaclr_object_load_field(struct zaclr_runtime* runtime,
                                            zaclr_object_handle handle,
                                            struct zaclr_token token,
                                            struct zaclr_stack_value* out_value);
uint32_t* zaclr_reference_object_field_tokens(struct zaclr_reference_object_desc* object);
const uint32_t* zaclr_reference_object_field_tokens_const(const struct zaclr_reference_object_desc* object);
struct zaclr_stack_value* zaclr_reference_object_field_values(struct zaclr_reference_object_desc* object);
const struct zaclr_stack_value* zaclr_reference_object_field_values_const(const struct zaclr_reference_object_desc* object);
uint32_t zaclr_stack_value_contains_references(const struct zaclr_stack_value* value);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_OBJECT_H */
