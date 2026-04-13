#ifndef ZACLR_EVAL_STACK_H
#define ZACLR_EVAL_STACK_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zaclr_stack_value_kind {
    ZACLR_STACK_VALUE_EMPTY = 0,
    ZACLR_STACK_VALUE_I4 = 1,
    ZACLR_STACK_VALUE_OBJECT_REFERENCE = 2,
    ZACLR_STACK_VALUE_I8 = 3,
    ZACLR_STACK_VALUE_R4 = 4,
    ZACLR_STACK_VALUE_R8 = 5,
    ZACLR_STACK_VALUE_LOCAL_ADDRESS = 6,
    ZACLR_STACK_VALUE_VALUETYPE = 7,
    ZACLR_STACK_VALUE_BYREF = 8
};

enum zaclr_stack_value_flags {
    ZACLR_STACK_VALUE_FLAG_NONE = 0x00000000u,
    ZACLR_STACK_VALUE_FLAG_OWNS_BUFFER = 0x00000001u,
    ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT = 0x00000002u
};

struct zaclr_object_desc;

#define ZACLR_STACK_VALUE_INLINE_BUFFER_BYTES 16u

struct zaclr_stack_value {
    uint32_t kind;
    uint32_t reserved;
    uint32_t payload_size;
    uint32_t type_token_raw;
    uint32_t flags;
    uint32_t extra;
    union {
        int32_t i4;
        int64_t i8;
        uint32_t r4_bits;
        uint64_t r8_bits;
        struct zaclr_object_desc* object_reference;
        void* bytes;
        uint8_t inline_bytes[ZACLR_STACK_VALUE_INLINE_BUFFER_BYTES];
        uintptr_t raw;
    } data;
};

struct zaclr_eval_stack {
    struct zaclr_stack_value* values;
    uint32_t depth;
    uint32_t capacity;
};

struct zaclr_result zaclr_eval_stack_initialize(struct zaclr_eval_stack* stack, uint32_t capacity);
void zaclr_eval_stack_destroy(struct zaclr_eval_stack* stack);
struct zaclr_result zaclr_eval_stack_push(struct zaclr_eval_stack* stack, const struct zaclr_stack_value* value);
struct zaclr_result zaclr_eval_stack_pop(struct zaclr_eval_stack* stack, struct zaclr_stack_value* value);
struct zaclr_result zaclr_eval_stack_peek(const struct zaclr_eval_stack* stack, struct zaclr_stack_value* value);
uint32_t zaclr_eval_stack_depth(const struct zaclr_eval_stack* stack);
void zaclr_stack_value_reset(struct zaclr_stack_value* value);
struct zaclr_result zaclr_stack_value_clone(struct zaclr_stack_value* destination,
                                            const struct zaclr_stack_value* source);
struct zaclr_result zaclr_stack_value_assign(struct zaclr_stack_value* destination,
                                             const struct zaclr_stack_value* source);
struct zaclr_result zaclr_stack_value_set_valuetype(struct zaclr_stack_value* value,
                                                    uint32_t type_token_raw,
                                                    const void* bytes,
                                                    uint32_t size);
struct zaclr_result zaclr_stack_value_set_byref(struct zaclr_stack_value* value,
                                                void* address,
                                                uint32_t payload_size,
                                                uint32_t type_token_raw,
                                                uint32_t flags);
void* zaclr_stack_value_payload(struct zaclr_stack_value* value);
const void* zaclr_stack_value_payload_const(const struct zaclr_stack_value* value);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_EVAL_STACK_H */
