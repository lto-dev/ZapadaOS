#include <kernel/zaclr/exec/zaclr_interop_dispatch.h>

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/exec/zaclr_call_resolution.h>
#include <kernel/zaclr/exec/zaclr_dispatch.h>
#include <kernel/zaclr/exec/zaclr_eval_stack.h>
#include <kernel/zaclr/exec/zaclr_frame.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/interop/zaclr_internal_call_registry.h>
#include <kernel/zaclr/interop/zaclr_marshalling.h>
#include <kernel/zaclr/interop/zaclr_qcall_table.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

extern "C" {
#include <kernel/support/kernel_memory.h>
}

extern "C" struct zaclr_result zaclr_allocate_reference_type_instance(struct zaclr_runtime* runtime,
                                                                       const struct zaclr_loaded_assembly* owning_assembly,
                                                                       struct zaclr_token type_token,
                                                                       struct zaclr_object_desc** out_object)
{
    zaclr_type_id type_id = 0u;
    uint32_t field_capacity = 0u;
    const struct zaclr_type_desc* type_desc = NULL;
    struct zaclr_reference_object_desc* object = NULL;

    auto accumulate_instance_field_capacity = [&](const struct zaclr_loaded_assembly* current_assembly,
                                                  const struct zaclr_type_desc* current_type,
                                                  uint32_t* io_capacity,
                                                  auto&& self) -> struct zaclr_result
    {
        const struct zaclr_loaded_assembly* base_assembly;
        const struct zaclr_type_desc* base_type;
        struct zaclr_result result;

        if (current_assembly == NULL || current_type == NULL || io_capacity == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *io_capacity += current_type->field_count;
        if (zaclr_token_is_nil(&current_type->extends))
        {
            return zaclr_result_ok();
        }

        result = zaclr_dispatch_resolve_type_desc(current_assembly,
                                                  runtime,
                                                  current_type->extends,
                                                  &base_assembly,
                                                  &base_type);
        if (result.status != ZACLR_STATUS_OK || base_type == NULL)
        {
            return result.status == ZACLR_STATUS_OK
                ? zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC)
                : result;
        }

        return self(base_assembly, base_type, io_capacity, self);
    };

    if (runtime == NULL || out_object == NULL || owning_assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    *out_object = NULL;
    if (zaclr_token_matches_table(&type_token, ZACLR_TOKEN_TABLE_TYPEDEF))
    {
        type_id = zaclr_token_row(&type_token);
        type_desc = zaclr_type_map_find_by_token(&owning_assembly->type_map, type_token);
        if (type_desc == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
        }

        {
            struct zaclr_result result = accumulate_instance_field_capacity(owning_assembly,
                                                                            type_desc,
                                                                            &field_capacity,
                                                                            accumulate_instance_field_capacity);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

        }
    }
    else if (zaclr_token_matches_table(&type_token, ZACLR_TOKEN_TABLE_TYPEREF)
             || zaclr_token_matches_table(&type_token, ZACLR_TOKEN_TABLE_TYPESPEC))
    {
        type_id = 0u;
    }
    else
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    {
        struct zaclr_result result = zaclr_reference_object_allocate(&runtime->heap,
                                                                    owning_assembly,
                                                                    type_id,
                                                                    type_token,
                                                                    field_capacity,
                                                                    &object);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (type_desc != NULL
            && (zaclr_type_runtime_flags(type_desc) & ZACLR_TYPE_RUNTIME_FLAG_HAS_FINALIZER) != 0u)
        {
            if (object != NULL)
            {
                object->object.gc_state = (uint8_t)(object->object.gc_state | ZACLR_OBJECT_GC_STATE_FINALIZER_PENDING);
            }
        }

        *out_object = &object->object;
        return zaclr_result_ok();
    }
}

extern "C" struct zaclr_result zaclr_invoke_internal_call_exact(struct zaclr_dispatch_context* context,
                                                                 struct zaclr_frame* frame,
                                                                 const struct zaclr_loaded_assembly* target_assembly,
                                                                 const struct zaclr_type_desc* target_type,
                                                                 const struct zaclr_method_desc* target_method,
                                                                 uint8_t invocation_kind)
{
    struct zaclr_internal_call_resolution internal_call = {};
    struct zaclr_native_call_frame native_frame = {};
    struct zaclr_result result;
    uint32_t total_arguments;
    uint32_t argument_index;
    struct zaclr_stack_value* stack_arguments = NULL;
    struct zaclr_stack_value* this_value = NULL;
    struct zaclr_stack_value* native_arguments = NULL;
    struct zaclr_stack_value injected_this = {};
    uint8_t has_this;

    if (context == NULL || frame == NULL || target_assembly == NULL || target_type == NULL || target_method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    ZACLR_TRACE_VALUE(context->runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "ExactBindToken",
                      (uint64_t)target_method->token.raw);
    ZACLR_TRACE_VALUE(context->runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      target_method->name.text,
                      (uint64_t)target_method->impl_flags);
    ZACLR_TRACE_VALUE(context->runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      target_method->pinvoke_import_name.text != NULL ? target_method->pinvoke_import_name.text : "<pinvoke-null>",
                      (uint64_t)target_method->signature.parameter_count);
    ZACLR_TRACE_VALUE(context->runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      target_method->pinvoke_module_name.text != NULL ? target_method->pinvoke_module_name.text : "<pinvoke-module-null>",
                      0u);

    result = zaclr_internal_call_registry_resolve_exact(&context->runtime->internal_calls,
                                                        target_assembly,
                                                        target_type,
                                                        target_method,
                                                        &internal_call);
    if (result.status != ZACLR_STATUS_OK)
    {
        ZACLR_TRACE_VALUE(context->runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "ExactBindFail",
                          (uint64_t)(((uint32_t)result.category << 16) | (uint32_t)result.status));
        return result;
    }

    ZACLR_TRACE_VALUE(context->runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_NATIVE_CALL,
                      target_method->name.text,
                      (uint64_t)target_method->token.raw);
    ZACLR_TRACE_VALUE(context->runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "Running method",
                      (uint64_t)target_method->token.raw);
    ZACLR_TRACE_VALUE(context->runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "ExactBind.StackDepth",
                      (uint64_t)zaclr_eval_stack_depth(&frame->eval_stack));
    if (zaclr_eval_stack_depth(&frame->eval_stack) != 0u)
    {
        struct zaclr_stack_value peek0 = {};
        if (zaclr_eval_stack_peek(&frame->eval_stack, &peek0).status == ZACLR_STATUS_OK)
        {
            ZACLR_TRACE_VALUE(context->runtime,
                              ZACLR_TRACE_CATEGORY_INTEROP,
                              ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                              "ExactBind.Peek0Kind",
                              (uint64_t)peek0.kind);
        }
        if (zaclr_eval_stack_depth(&frame->eval_stack) > 1u)
        {
            ZACLR_TRACE_VALUE(context->runtime,
                              ZACLR_TRACE_CATEGORY_INTEROP,
                              ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                              "ExactBind.Peek1Kind",
                              (uint64_t)frame->eval_stack.values[frame->eval_stack.depth - 2u].kind);
        }
        if (zaclr_eval_stack_depth(&frame->eval_stack) > 2u)
        {
            ZACLR_TRACE_VALUE(context->runtime,
                              ZACLR_TRACE_CATEGORY_INTEROP,
                              ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                              "ExactBind.Peek2Kind",
                              (uint64_t)frame->eval_stack.values[frame->eval_stack.depth - 3u].kind);
        }
    }

    has_this = (target_method->signature.calling_convention & 0x20u) != 0u ? 1u : 0u;
    total_arguments = target_method->signature.parameter_count + ((has_this != 0u && invocation_kind != ZACLR_NATIVE_CALL_INVOCATION_NEWOBJ) ? 1u : 0u);
    if (total_arguments != 0u)
    {
        stack_arguments = (struct zaclr_stack_value*)kernel_alloc(sizeof(struct zaclr_stack_value) * total_arguments);
        if (stack_arguments == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
        }
    }

    for (argument_index = total_arguments; argument_index > 0u; --argument_index)
    {
        result = zaclr_eval_stack_pop(&frame->eval_stack, &stack_arguments[argument_index - 1u]);
        if (result.status != ZACLR_STATUS_OK)
        {
            if (stack_arguments != NULL)
            {
                kernel_free(stack_arguments);
            }
            return result;
        }
    }

    if (invocation_kind == ZACLR_NATIVE_CALL_INVOCATION_NEWOBJ && has_this != 0u)
    {
        struct zaclr_object_desc* instance_object = NULL;
        result = zaclr_allocate_reference_type_instance(context->runtime,
                                                        target_assembly,
                                                        target_method->owning_type_token,
                                                        &instance_object);
        if (result.status != ZACLR_STATUS_OK)
        {
            if (stack_arguments != NULL)
            {
                kernel_free(stack_arguments);
            }
            return result;
        }

        injected_this.kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        injected_this.data.object_reference = instance_object;
        this_value = &injected_this;
        native_arguments = stack_arguments;
    }
    else if (has_this != 0u && stack_arguments != NULL)
    {
        ZACLR_TRACE_VALUE(context->runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "ExactBind.ThisKind",
                          (uint64_t)stack_arguments[0].kind);
        this_value = &stack_arguments[0];
        native_arguments = &stack_arguments[1];
    }

    if (target_method->signature.parameter_count != 0u && stack_arguments != NULL)
    {
        ZACLR_TRACE_VALUE(context->runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "ExactBind.Arg0Kind",
                          (uint64_t)stack_arguments[(has_this != 0u && invocation_kind != ZACLR_NATIVE_CALL_INVOCATION_NEWOBJ) ? 1u : 0u].kind);
    }

    if (has_this == 0u)
    {
        native_arguments = stack_arguments;
    }

    result = zaclr_build_native_call_frame(context->runtime,
                                           frame,
                                           target_assembly,
                                           target_method,
                                           invocation_kind,
                                           has_this,
                                           this_value,
                                           (uint8_t)target_method->signature.parameter_count,
                                           native_arguments,
                                           &native_frame);
    if (result.status != ZACLR_STATUS_OK)
    {
        if (stack_arguments != NULL)
        {
            kernel_free(stack_arguments);
        }
        return result;
    }

    result = internal_call.method->handler != NULL
        ? internal_call.method->handler(native_frame)
        : zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_INTEROP);
    if (stack_arguments != NULL)
    {
        kernel_free(stack_arguments);
    }
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (native_frame.has_result == 0u)
    {
        return zaclr_result_ok();
    }

    return zaclr_eval_stack_push(&frame->eval_stack, &native_frame.result_value);
}

extern "C" struct zaclr_result zaclr_invoke_native_frame_handler_exact(struct zaclr_dispatch_context* context,
                                                                        struct zaclr_frame* frame,
                                                                        const struct zaclr_loaded_assembly* owning_assembly,
                                                                        const struct zaclr_method_desc* method,
                                                                        zaclr_native_frame_handler handler,
                                                                        uint8_t invocation_kind)
{
    struct zaclr_native_call_frame native_frame = {};
    struct zaclr_result result;
    uint32_t total_arguments;
    uint32_t argument_index;
    struct zaclr_stack_value* stack_arguments = NULL;
    struct zaclr_stack_value* this_value = NULL;
    struct zaclr_stack_value* native_arguments = NULL;
    struct zaclr_stack_value injected_this = {};
    uint8_t has_this;

    if (context == NULL || frame == NULL || owning_assembly == NULL || method == NULL || handler == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    has_this = (method->signature.calling_convention & 0x20u) != 0u ? 1u : 0u;
    total_arguments = method->signature.parameter_count + ((has_this != 0u && invocation_kind != ZACLR_NATIVE_CALL_INVOCATION_NEWOBJ) ? 1u : 0u);
    if (total_arguments != 0u)
    {
        stack_arguments = (struct zaclr_stack_value*)kernel_alloc(sizeof(struct zaclr_stack_value) * total_arguments);
        if (stack_arguments == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
        }
    }

    for (argument_index = total_arguments; argument_index > 0u; --argument_index)
    {
        result = zaclr_eval_stack_pop(&frame->eval_stack, &stack_arguments[argument_index - 1u]);
        if (result.status != ZACLR_STATUS_OK)
        {
            if (stack_arguments != NULL)
            {
                kernel_free(stack_arguments);
            }
            return result;
        }
    }

    if (invocation_kind == ZACLR_NATIVE_CALL_INVOCATION_NEWOBJ && has_this != 0u)
    {
        struct zaclr_object_desc* instance_object = NULL;
        result = zaclr_allocate_reference_type_instance(context->runtime,
                                                        owning_assembly,
                                                        method->owning_type_token,
                                                        &instance_object);
        if (result.status != ZACLR_STATUS_OK)
        {
            if (stack_arguments != NULL)
            {
                kernel_free(stack_arguments);
            }
            return result;
        }

        injected_this.kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        injected_this.data.object_reference = instance_object;
        this_value = &injected_this;
        native_arguments = stack_arguments;
    }
    else if (has_this != 0u && stack_arguments != NULL)
    {
        this_value = &stack_arguments[0];
        native_arguments = &stack_arguments[1];
    }

    if (has_this == 0u)
    {
        native_arguments = stack_arguments;
    }

    result = zaclr_build_native_call_frame(context->runtime,
                                           frame,
                                           owning_assembly,
                                           method,
                                           invocation_kind,
                                           has_this,
                                           this_value,
                                           (uint8_t)method->signature.parameter_count,
                                           native_arguments,
                                           &native_frame);
    if (result.status != ZACLR_STATUS_OK)
    {
        if (stack_arguments != NULL)
        {
            kernel_free(stack_arguments);
        }
        return result;
    }

    result = handler(native_frame);
    if (stack_arguments != NULL)
    {
        kernel_free(stack_arguments);
    }
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (native_frame.has_result == 0u)
    {
        return zaclr_result_ok();
    }

    return zaclr_eval_stack_push(&frame->eval_stack, &native_frame.result_value);
}
