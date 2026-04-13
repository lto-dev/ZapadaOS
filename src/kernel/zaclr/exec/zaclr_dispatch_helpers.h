#ifndef ZACLR_DISPATCH_HELPERS_H
#define ZACLR_DISPATCH_HELPERS_H

#include <kernel/zaclr/exec/zaclr_eval_stack.h>
#include <kernel/zaclr/exec/zaclr_frame.h>
#include <kernel/zaclr/include/zaclr_status.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

#ifdef __cplusplus

static inline struct zaclr_stack_value* zaclr_dispatch_resolve_local_address_target(struct zaclr_frame* frame,
                                                                                    struct zaclr_stack_value* value)
{
    uint32_t local_index;

    if (frame == NULL || value == NULL || value->kind != ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        return NULL;
    }

    local_index = (uint32_t)value->data.raw;
    if (local_index < frame->local_count)
    {
        return &frame->locals[local_index];
    }

    return (struct zaclr_stack_value*)(uintptr_t)value->data.raw;
}

static inline struct zaclr_result zaclr_dispatch_push_object_reference(struct zaclr_frame* frame,
                                                                      struct zaclr_object_desc* value)
{
    struct zaclr_stack_value stack_value = {};
    stack_value.kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
    stack_value.data.object_reference = value;
    return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
}

static inline struct zaclr_result zaclr_dispatch_push_object_handle(struct zaclr_frame* frame,
                                                                    zaclr_object_handle value)
{
    return zaclr_dispatch_push_object_reference(frame,
                                                frame != NULL && frame->runtime != NULL
                                                    ? zaclr_heap_get_object(&frame->runtime->heap, value)
                                                    : NULL);
}

static inline struct zaclr_result zaclr_dispatch_push_i4(struct zaclr_frame* frame, int32_t value)
{
    struct zaclr_stack_value stack_value = {};
    stack_value.kind = ZACLR_STACK_VALUE_I4;
    stack_value.data.i4 = value;
    return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
}

static inline bool zaclr_dispatch_text_equals(const char* left, const char* right)
{
    return zaclr_text_equals(left, right);
}

#endif /* __cplusplus */

#endif /* ZACLR_DISPATCH_HELPERS_H */
