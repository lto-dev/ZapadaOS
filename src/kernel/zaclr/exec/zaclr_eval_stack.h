#ifndef ZACLR_EVAL_STACK_H
#define ZACLR_EVAL_STACK_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zaclr_stack_value_kind {
    ZACLR_STACK_VALUE_EMPTY = 0,
    ZACLR_STACK_VALUE_I4 = 1,
    ZACLR_STACK_VALUE_OBJECT_HANDLE = 2,
    ZACLR_STACK_VALUE_I8 = 3,
    ZACLR_STACK_VALUE_R4 = 4,
    ZACLR_STACK_VALUE_R8 = 5,
    ZACLR_STACK_VALUE_LOCAL_ADDRESS = 6
};

struct zaclr_stack_value {
    uint32_t kind;
    uint32_t reserved;
    union {
        int32_t i4;
        int64_t i8;
        uint32_t r4_bits;
        uint64_t r8_bits;
        zaclr_object_handle object_handle;
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

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_EVAL_STACK_H */
