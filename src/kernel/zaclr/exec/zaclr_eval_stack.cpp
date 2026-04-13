#include <kernel/zaclr/exec/zaclr_eval_stack.h>

#include <kernel/support/kernel_memory.h>

namespace
{
    static bool stack_value_uses_owned_buffer(const struct zaclr_stack_value* value)
    {
        return value != NULL
            && value->kind == ZACLR_STACK_VALUE_VALUETYPE
            && (value->flags & ZACLR_STACK_VALUE_FLAG_OWNS_BUFFER) != 0u
            && value->data.bytes != NULL;
    }
}

extern "C" void zaclr_stack_value_reset(struct zaclr_stack_value* value)
{
    if (value == NULL)
    {
        return;
    }

    if (stack_value_uses_owned_buffer(value))
    {
        kernel_free(value->data.bytes);
    }

    *value = {};
}

extern "C" void* zaclr_stack_value_payload(struct zaclr_stack_value* value)
{
    if (value == NULL || value->kind != ZACLR_STACK_VALUE_VALUETYPE)
    {
        return NULL;
    }

    return (value->flags & ZACLR_STACK_VALUE_FLAG_OWNS_BUFFER) != 0u
        ? value->data.bytes
        : (void*)value->data.inline_bytes;
}

extern "C" const void* zaclr_stack_value_payload_const(const struct zaclr_stack_value* value)
{
    if (value == NULL || value->kind != ZACLR_STACK_VALUE_VALUETYPE)
    {
        return NULL;
    }

    return (value->flags & ZACLR_STACK_VALUE_FLAG_OWNS_BUFFER) != 0u
        ? value->data.bytes
        : (const void*)value->data.inline_bytes;
}

extern "C" struct zaclr_result zaclr_stack_value_set_valuetype(struct zaclr_stack_value* value,
                                                                  uint32_t type_token_raw,
                                                                  const void* bytes,
                                                                  uint32_t size)
{
    void* storage;

    if (value == NULL || (size != 0u && bytes == NULL))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    zaclr_stack_value_reset(value);
    value->kind = ZACLR_STACK_VALUE_VALUETYPE;
    value->payload_size = size;
    value->type_token_raw = type_token_raw;

    if (size == 0u)
    {
        return zaclr_result_ok();
    }

    if (size <= ZACLR_STACK_VALUE_INLINE_BUFFER_BYTES)
    {
        storage = value->data.inline_bytes;
        value->flags = ZACLR_STACK_VALUE_FLAG_NONE;
    }
    else
    {
        storage = kernel_alloc(size);
        if (storage == NULL)
        {
            zaclr_stack_value_reset(value);
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_EXEC);
        }

        value->data.bytes = storage;
        value->flags = ZACLR_STACK_VALUE_FLAG_OWNS_BUFFER;
    }

    kernel_memcpy(storage, bytes, size);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_stack_value_set_byref(struct zaclr_stack_value* value,
                                                              void* address,
                                                              uint32_t payload_size,
                                                              uint32_t type_token_raw,
                                                              uint32_t flags)
{
    if (value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    zaclr_stack_value_reset(value);
    value->kind = ZACLR_STACK_VALUE_BYREF;
    value->payload_size = payload_size;
    value->type_token_raw = type_token_raw;
    value->flags = flags & ~ZACLR_STACK_VALUE_FLAG_OWNS_BUFFER;
    value->data.raw = (uintptr_t)address;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_stack_value_clone(struct zaclr_stack_value* destination,
                                                          const struct zaclr_stack_value* source)
{
    if (destination == NULL || source == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    *destination = {};
    if (source->kind != ZACLR_STACK_VALUE_VALUETYPE)
    {
        *destination = *source;
        return zaclr_result_ok();
    }

    return zaclr_stack_value_set_valuetype(destination,
                                           source->type_token_raw,
                                           zaclr_stack_value_payload_const(source),
                                           source->payload_size);
}

extern "C" struct zaclr_result zaclr_stack_value_assign(struct zaclr_stack_value* destination,
                                                           const struct zaclr_stack_value* source)
{
    struct zaclr_result result;

    if (destination == NULL || source == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    zaclr_stack_value_reset(destination);
    result = zaclr_stack_value_clone(destination, source);
    if (result.status != ZACLR_STATUS_OK)
    {
        zaclr_stack_value_reset(destination);
    }

    return result;
}

extern "C" struct zaclr_result zaclr_eval_stack_initialize(struct zaclr_eval_stack* stack,
                                                             uint32_t capacity)
{
    if (stack == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    *stack = {};
    if (capacity == 0u)
    {
        return zaclr_result_ok();
    }

    stack->values = (struct zaclr_stack_value*)kernel_alloc(sizeof(struct zaclr_stack_value) * capacity);
    if (stack->values == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_EXEC);
    }

    kernel_memset(stack->values, 0, sizeof(struct zaclr_stack_value) * capacity);
    stack->capacity = capacity;
    return zaclr_result_ok();
}

extern "C" void zaclr_eval_stack_destroy(struct zaclr_eval_stack* stack)
{
    uint32_t index;

    if (stack == NULL)
    {
        return;
    }

    if (stack->values != NULL)
    {
        for (index = 0u; index < stack->capacity; ++index)
        {
            zaclr_stack_value_reset(&stack->values[index]);
        }

        kernel_free(stack->values);
    }

    *stack = {};
}

extern "C" struct zaclr_result zaclr_eval_stack_push(struct zaclr_eval_stack* stack, const struct zaclr_stack_value* value)
{
    if (stack == NULL || value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (stack->depth >= stack->capacity)
    {
        return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
    }

    {
        struct zaclr_result result = zaclr_stack_value_assign(&stack->values[stack->depth], value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
    }

    stack->depth++;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_eval_stack_pop(struct zaclr_eval_stack* stack, struct zaclr_stack_value* value)
{
    if (stack == NULL || value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (stack->depth == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    *value = stack->values[stack->depth - 1u];
    stack->values[stack->depth - 1u] = {};
    stack->depth--;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_eval_stack_peek(const struct zaclr_eval_stack* stack, struct zaclr_stack_value* value)
{
    if (stack == NULL || value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (stack->depth == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    return zaclr_stack_value_assign(value, &stack->values[stack->depth - 1u]);
}

extern "C" uint32_t zaclr_eval_stack_depth(const struct zaclr_eval_stack* stack)
{
    return stack != NULL ? stack->depth : 0u;
}
