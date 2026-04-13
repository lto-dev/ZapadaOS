#ifndef ZACLR_OBJECT_H
#define ZACLR_OBJECT_H

#include <kernel/zaclr/exec/zaclr_eval_stack.h>
#include <kernel/zaclr/include/zaclr_public_api.h>
#include <kernel/zaclr/metadata/zaclr_token.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_method_table;

struct zaclr_object_header {
    uint32_t sync_block_index;
    uint32_t flags;
    struct zaclr_method_table* method_table;
};

struct zaclr_object_desc {
    uint32_t size_bytes;
    uint16_t family;
    uint8_t gc_mark;
    uint8_t gc_state;
    struct zaclr_object_header header;
    const struct zaclr_loaded_assembly* owning_assembly;
    zaclr_type_id type_id;
};

struct zaclr_boxed_value_desc {
    struct zaclr_object_desc object;
    uint32_t type_token_raw;
    uint32_t reserved;
    struct zaclr_stack_value value;
};

struct zaclr_reference_object_desc {
    struct zaclr_object_desc object;
    uint32_t compatibility_field_capacity;
    uint32_t reserved;
};

struct zaclr_runtime_type_desc {
    struct zaclr_object_desc object;
    const struct zaclr_loaded_assembly* type_assembly;
    struct zaclr_token type_token;
};

struct zaclr_runtime;
struct zaclr_loaded_assembly;

enum zaclr_object_family {
    ZACLR_OBJECT_FAMILY_UNKNOWN = 0,
    ZACLR_OBJECT_FAMILY_INSTANCE = 1,
    ZACLR_OBJECT_FAMILY_ARRAY = 2,
    ZACLR_OBJECT_FAMILY_STRING = 3,
    ZACLR_OBJECT_FAMILY_RUNTIME_TYPE = 4
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
    ZACLR_OBJECT_GC_STATE_FINALIZER_COMPLETE = 0x08u,
    ZACLR_OBJECT_GC_STATE_FINALIZER_SUPPRESSED = 0x10u
};

uint32_t zaclr_object_flags(const struct zaclr_object_desc* object);
uint32_t zaclr_object_size_bytes(const struct zaclr_object_desc* object);
uint32_t zaclr_object_family(const struct zaclr_object_desc* object);
uint32_t zaclr_object_contains_references(const struct zaclr_object_desc* object);
uint32_t zaclr_object_is_marked(const struct zaclr_object_desc* object);
void zaclr_object_set_marked(struct zaclr_object_desc* object, uint32_t marked);
struct zaclr_method_table* zaclr_object_method_table(struct zaclr_object_desc* object);
const struct zaclr_method_table* zaclr_object_method_table_const(const struct zaclr_object_desc* object);
const struct zaclr_loaded_assembly* zaclr_object_owning_assembly(const struct zaclr_object_desc* object);
zaclr_type_id zaclr_object_type_id(const struct zaclr_object_desc* object);
struct zaclr_result zaclr_boxed_value_allocate(struct zaclr_heap* heap,
                                               struct zaclr_token token,
                                               const struct zaclr_stack_value* value,
                                               struct zaclr_boxed_value_desc** out_value);
struct zaclr_result zaclr_boxed_value_allocate_handle(struct zaclr_heap* heap,
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
                                                    struct zaclr_reference_object_desc** out_object);
struct zaclr_result zaclr_reference_object_allocate_handle(struct zaclr_heap* heap,
                                                           const struct zaclr_loaded_assembly* owning_assembly,
                                                           zaclr_type_id type_id,
                                                           struct zaclr_token type_token,
                                                           uint32_t field_capacity,
                                                           zaclr_object_handle* out_handle);
struct zaclr_result zaclr_runtime_type_allocate(struct zaclr_heap* heap,
                                                const struct zaclr_loaded_assembly* type_assembly,
                                                struct zaclr_token type_token,
                                                struct zaclr_runtime_type_desc** out_runtime_type);
struct zaclr_result zaclr_runtime_type_allocate_handle(struct zaclr_heap* heap,
                                                       const struct zaclr_loaded_assembly* type_assembly,
                                                       struct zaclr_token type_token,
                                                       zaclr_object_handle* out_handle);
struct zaclr_reference_object_desc* zaclr_reference_object_from_handle(struct zaclr_heap* heap,
                                                                       zaclr_object_handle handle);
struct zaclr_runtime_type_desc* zaclr_runtime_type_from_handle(struct zaclr_heap* heap,
                                                               zaclr_object_handle handle);
const struct zaclr_reference_object_desc* zaclr_reference_object_from_handle_const(const struct zaclr_heap* heap,
                                                                                  zaclr_object_handle handle);
const struct zaclr_runtime_type_desc* zaclr_runtime_type_from_handle_const(const struct zaclr_heap* heap,
                                                                           zaclr_object_handle handle);
struct zaclr_result zaclr_reference_object_store_field(struct zaclr_reference_object_desc* object,
                                                       struct zaclr_token token,
                                                       const struct zaclr_stack_value* value);
struct zaclr_result zaclr_reference_object_load_field(const struct zaclr_reference_object_desc* object,
                                                      struct zaclr_token token,
                                                      struct zaclr_stack_value* out_value);
const struct zaclr_field_layout* zaclr_reference_object_field_layout(const struct zaclr_reference_object_desc* object,
                                                                     struct zaclr_token token);
void* zaclr_reference_object_field_address(struct zaclr_reference_object_desc* object,
                                          struct zaclr_token token);
const void* zaclr_reference_object_field_address_const(const struct zaclr_reference_object_desc* object,
                                                       struct zaclr_token token);
struct zaclr_stack_value* zaclr_reference_object_field_storage(struct zaclr_reference_object_desc* object,
                                                               struct zaclr_token token);
const struct zaclr_stack_value* zaclr_reference_object_field_storage_const(const struct zaclr_reference_object_desc* object,
                                                                           struct zaclr_token token);
struct zaclr_result zaclr_object_store_field(struct zaclr_runtime* runtime,
                                             struct zaclr_object_desc* object,
                                             struct zaclr_token token,
                                             const struct zaclr_stack_value* value);
struct zaclr_result zaclr_object_store_field_handle(struct zaclr_runtime* runtime,
                                                    zaclr_object_handle handle,
                                                    struct zaclr_token token,
                                                    const struct zaclr_stack_value* value);
struct zaclr_result zaclr_object_load_field(struct zaclr_runtime* runtime,
                                            const struct zaclr_object_desc* object,
                                            struct zaclr_token token,
                                            struct zaclr_stack_value* out_value);
struct zaclr_result zaclr_object_load_field_handle(struct zaclr_runtime* runtime,
                                                   zaclr_object_handle handle,
                                                   struct zaclr_token token,
                                                   struct zaclr_stack_value* out_value);
struct zaclr_result zaclr_boxed_value_load_field(struct zaclr_runtime* runtime,
                                                 const struct zaclr_boxed_value_desc* boxed_value,
                                                 struct zaclr_token token,
                                                 struct zaclr_stack_value* out_value);
struct zaclr_result zaclr_boxed_value_store_field(struct zaclr_runtime* runtime,
                                                  struct zaclr_boxed_value_desc* boxed_value,
                                                  struct zaclr_token token,
                                                  const struct zaclr_stack_value* value);
uint32_t zaclr_stack_value_contains_references(const struct zaclr_stack_value* value);
void zaclr_gc_write_barrier(struct zaclr_object_desc** slot,
                            struct zaclr_object_desc* value);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_OBJECT_H */
