#include <kernel/zaclr/exec/zaclr_eval_stack.h>

#include <kernel/support/kernel_memory.h>

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
    if (stack == NULL)
    {
        return;
    }

    if (stack->values != NULL)
    {
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

    stack->values[stack->depth++] = *value;
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

    *value = stack->values[stack->depth - 1u];
    return zaclr_result_ok();
}

extern "C" uint32_t zaclr_eval_stack_depth(const struct zaclr_eval_stack* stack)
{
    return stack != NULL ? stack->depth : 0u;
}
