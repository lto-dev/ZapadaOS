#include <kernel/zaclr/interop/zaclr_marshalling.h>

#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/include/zaclr_trace.h>
#include <kernel/zaclr/heap/zaclr_array.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/metadata/zaclr_method_map.h>
#include <kernel/zaclr/interop/zaclr_internal_call_registry.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

namespace
{
    static struct zaclr_result invalid_arg()
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    static struct zaclr_result not_found_heap()
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    static struct zaclr_stack_value* resolve_byref_target(struct zaclr_stack_value* value)
    {
        if (value == NULL || value->kind != ZACLR_STACK_VALUE_LOCAL_ADDRESS)
        {
            return NULL;
        }

        return (struct zaclr_stack_value*)(uintptr_t)value->data.raw;
    }
}

extern "C" uint8_t zaclr_native_call_frame_argument_count(const struct zaclr_native_call_frame* frame)
{
    return frame != NULL ? frame->argument_count : 0u;
}

extern "C" uint8_t zaclr_native_call_frame_has_this(const struct zaclr_native_call_frame* frame)
{
    return frame != NULL ? frame->has_this : 0u;
}

extern "C" uint8_t zaclr_native_call_frame_invocation_kind(const struct zaclr_native_call_frame* frame)
{
    return frame != NULL ? frame->invocation_kind : 0u;
}

extern "C" struct zaclr_stack_value* zaclr_native_call_frame_this(struct zaclr_native_call_frame* frame)
{
    return frame != NULL ? frame->this_value : NULL;
}

extern "C" struct zaclr_stack_value* zaclr_native_call_frame_arg(struct zaclr_native_call_frame* frame,
                                                                  uint32_t index)
{
    if (frame == NULL || frame->arguments == NULL || index >= frame->argument_count)
    {
        return NULL;
    }

    return &frame->arguments[index];
}

extern "C" struct zaclr_result zaclr_native_call_frame_arg_i4(struct zaclr_native_call_frame* frame,
                                                               uint32_t index,
                                                               int32_t* out_value)
{
    struct zaclr_stack_value* value = zaclr_native_call_frame_arg(frame, index);
    if (value == NULL || out_value == NULL)
    {
        return invalid_arg();
    }

    if (value->kind == ZACLR_STACK_VALUE_I4)
    {
        *out_value = value->data.i4;
        return zaclr_result_ok();
    }

    if (value->kind == ZACLR_STACK_VALUE_I8)
    {
        *out_value = (int32_t)value->data.i8;
        return zaclr_result_ok();
    }

    return invalid_arg();
}

extern "C" struct zaclr_result zaclr_native_call_frame_arg_i8(struct zaclr_native_call_frame* frame,
                                                               uint32_t index,
                                                               int64_t* out_value)
{
    struct zaclr_stack_value* value = zaclr_native_call_frame_arg(frame, index);
    if (value == NULL || out_value == NULL)
    {
        return invalid_arg();
    }

    if (value->kind == ZACLR_STACK_VALUE_I8)
    {
        *out_value = value->data.i8;
        return zaclr_result_ok();
    }

    if (value->kind == ZACLR_STACK_VALUE_I4)
    {
        *out_value = value->data.i4;
        return zaclr_result_ok();
    }

    return invalid_arg();
}

extern "C" struct zaclr_result zaclr_native_call_frame_arg_bool(struct zaclr_native_call_frame* frame,
                                                                 uint32_t index,
                                                                 bool* out_value)
{
    int32_t value = 0;
    struct zaclr_result result = zaclr_native_call_frame_arg_i4(frame, index, &value);
    if (result.status != ZACLR_STATUS_OK || out_value == NULL)
    {
        return result.status == ZACLR_STATUS_OK ? invalid_arg() : result;
    }

    *out_value = value != 0;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_native_call_frame_arg_object(struct zaclr_native_call_frame* frame,
                                                                    uint32_t index,
                                                                    zaclr_object_handle* out_value)
{
    struct zaclr_stack_value* value = zaclr_native_call_frame_arg(frame, index);
    if (value == NULL || out_value == NULL || value->kind != ZACLR_STACK_VALUE_OBJECT_HANDLE)
    {
        if (frame != NULL && frame->runtime != NULL)
        {
            ZACLR_TRACE_VALUE(frame->runtime,
                              ZACLR_TRACE_CATEGORY_INTEROP,
                              ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                              "ArgObject.FrameArgCount",
                              (uint64_t)frame->argument_count);
            ZACLR_TRACE_VALUE(frame->runtime,
                              ZACLR_TRACE_CATEGORY_INTEROP,
                              ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                              "ArgObject.FrameArgsPtr",
                              (uint64_t)(uintptr_t)frame->arguments);
            ZACLR_TRACE_VALUE(frame->runtime,
                              ZACLR_TRACE_CATEGORY_INTEROP,
                              ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                              "ArgObject.RequestedIndex",
                              (uint64_t)index);
            ZACLR_TRACE_VALUE(frame->runtime,
                              ZACLR_TRACE_CATEGORY_INTEROP,
                              ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                              "ArgObject.ValueKind",
                              (uint64_t)(value != NULL ? value->kind : 0xFFFFFFFFu));
        }
        return invalid_arg();
    }

    *out_value = value->data.object_handle;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_native_call_frame_arg_string(struct zaclr_native_call_frame* frame,
                                                                   uint32_t index,
                                                                   const struct zaclr_string_desc** out_value)
{
    zaclr_object_handle handle = 0u;
    struct zaclr_result result = zaclr_native_call_frame_arg_object(frame, index, &handle);
    if (result.status != ZACLR_STATUS_OK || out_value == NULL)
    {
        return result.status == ZACLR_STATUS_OK ? invalid_arg() : result;
    }

    *out_value = handle == 0u ? NULL : zaclr_string_from_handle_const(&frame->runtime->heap, handle);
    return handle != 0u && *out_value == NULL ? not_found_heap() : zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_native_call_frame_arg_array(struct zaclr_native_call_frame* frame,
                                                                  uint32_t index,
                                                                  const struct zaclr_array_desc** out_value)
{
    zaclr_object_handle handle = 0u;
    struct zaclr_result result = zaclr_native_call_frame_arg_object(frame, index, &handle);
    if (result.status != ZACLR_STATUS_OK || out_value == NULL)
    {
        return result.status == ZACLR_STATUS_OK ? invalid_arg() : result;
    }

    *out_value = handle == 0u ? NULL : zaclr_array_from_handle_const(&frame->runtime->heap, handle);
    return handle != 0u && *out_value == NULL ? not_found_heap() : zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_native_call_frame_load_byref_i4(struct zaclr_native_call_frame* frame,
                                                                       uint32_t index,
                                                                       int32_t* out_value)
{
    struct zaclr_stack_value* arg = zaclr_native_call_frame_arg(frame, index);
    struct zaclr_stack_value* target = resolve_byref_target(arg);

    if (target == NULL && frame != NULL && frame->caller_frame != NULL && arg != NULL && arg->kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        uint32_t local_index = (uint32_t)arg->data.raw;
        if (local_index < frame->caller_frame->local_count)
        {
            target = &frame->caller_frame->locals[local_index];
        }
    }

    if (target == NULL || out_value == NULL)
    {
        return invalid_arg();
    }

    if (target->kind == ZACLR_STACK_VALUE_I4)
    {
        *out_value = target->data.i4;
        return zaclr_result_ok();
    }

    if (target->kind == ZACLR_STACK_VALUE_I8)
    {
        *out_value = (int32_t)target->data.i8;
        return zaclr_result_ok();
    }

    return invalid_arg();
}

extern "C" struct zaclr_result zaclr_native_call_frame_store_byref_i4(struct zaclr_native_call_frame* frame,
                                                                         uint32_t index,
                                                                         int32_t value)
{
    struct zaclr_stack_value* arg = zaclr_native_call_frame_arg(frame, index);
    struct zaclr_stack_value* target = resolve_byref_target(arg);

    if (target == NULL && frame != NULL && frame->caller_frame != NULL && arg != NULL && arg->kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        uint32_t local_index = (uint32_t)arg->data.raw;
        if (local_index < frame->caller_frame->local_count)
        {
            target = &frame->caller_frame->locals[local_index];
        }
    }

    if (target == NULL)
    {
        return invalid_arg();
    }

    target->kind = ZACLR_STACK_VALUE_I4;
    target->reserved = 0u;
    target->data.i4 = value;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_native_call_frame_load_byref_i8(struct zaclr_native_call_frame* frame,
                                                                       uint32_t index,
                                                                       int64_t* out_value)
{
    struct zaclr_stack_value* arg = zaclr_native_call_frame_arg(frame, index);
    struct zaclr_stack_value* target = resolve_byref_target(arg);

    if (target == NULL && frame != NULL && frame->caller_frame != NULL && arg != NULL && arg->kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        uint32_t local_index = (uint32_t)arg->data.raw;
        if (local_index < frame->caller_frame->local_count)
        {
            target = &frame->caller_frame->locals[local_index];
        }
    }

    if (target == NULL || out_value == NULL)
    {
        return invalid_arg();
    }

    if (target->kind == ZACLR_STACK_VALUE_I8)
    {
        *out_value = target->data.i8;
        return zaclr_result_ok();
    }

    if (target->kind == ZACLR_STACK_VALUE_I4)
    {
        *out_value = target->data.i4;
        return zaclr_result_ok();
    }

    return invalid_arg();
}

extern "C" struct zaclr_result zaclr_native_call_frame_store_byref_i8(struct zaclr_native_call_frame* frame,
                                                                        uint32_t index,
                                                                        int64_t value)
{
    struct zaclr_stack_value* arg = zaclr_native_call_frame_arg(frame, index);
    struct zaclr_stack_value* target = resolve_byref_target(arg);

    if (target == NULL && frame != NULL && frame->caller_frame != NULL && arg != NULL && arg->kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        uint32_t local_index = (uint32_t)arg->data.raw;
        if (local_index < frame->caller_frame->local_count)
        {
            target = &frame->caller_frame->locals[local_index];
        }
    }

    if (target == NULL)
    {
        return invalid_arg();
    }

    target->kind = ZACLR_STACK_VALUE_I8;
    target->reserved = 0u;
    target->data.i8 = value;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_native_call_frame_load_byref_object(struct zaclr_native_call_frame* frame,
                                                                           uint32_t index,
                                                                           zaclr_object_handle* out_value)
{
    struct zaclr_stack_value* arg = zaclr_native_call_frame_arg(frame, index);
    struct zaclr_stack_value* target = resolve_byref_target(arg);

    if (target == NULL && frame != NULL && frame->caller_frame != NULL && arg != NULL && arg->kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        uint32_t local_index = (uint32_t)arg->data.raw;
        if (local_index < frame->caller_frame->local_count)
        {
            target = &frame->caller_frame->locals[local_index];
        }
    }

    if (target == NULL || out_value == NULL || target->kind != ZACLR_STACK_VALUE_OBJECT_HANDLE)
    {
        return invalid_arg();
    }

    *out_value = target->data.object_handle;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_native_call_frame_store_byref_object(struct zaclr_native_call_frame* frame,
                                                                            uint32_t index,
                                                                            zaclr_object_handle value)
{
    struct zaclr_stack_value* arg = zaclr_native_call_frame_arg(frame, index);
    struct zaclr_stack_value* target = resolve_byref_target(arg);

    if (target == NULL && frame != NULL && frame->caller_frame != NULL && arg != NULL && arg->kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        uint32_t local_index = (uint32_t)arg->data.raw;
        if (local_index < frame->caller_frame->local_count)
        {
            target = &frame->caller_frame->locals[local_index];
        }
    }

    if (target == NULL)
    {
        return invalid_arg();
    }

    target->kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
    target->reserved = 0u;
    target->data.object_handle = value;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_native_call_frame_set_void(struct zaclr_native_call_frame* frame)
{
    if (frame == NULL)
    {
        return invalid_arg();
    }

    frame->has_result = 0u;
    frame->result_value = {};
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_native_call_frame_set_i4(struct zaclr_native_call_frame* frame,
                                                               int32_t value)
{
    if (frame == NULL)
    {
        return invalid_arg();
    }

    frame->has_result = 1u;
    frame->result_value = {};
    frame->result_value.kind = ZACLR_STACK_VALUE_I4;
    frame->result_value.data.i4 = value;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_native_call_frame_set_i8(struct zaclr_native_call_frame* frame,
                                                               int64_t value)
{
    if (frame == NULL)
    {
        return invalid_arg();
    }

    frame->has_result = 1u;
    frame->result_value = {};
    frame->result_value.kind = ZACLR_STACK_VALUE_I8;
    frame->result_value.data.i8 = value;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_native_call_frame_set_bool(struct zaclr_native_call_frame* frame,
                                                                 bool value)
{
    return zaclr_native_call_frame_set_i4(frame, value ? 1 : 0);
}

extern "C" struct zaclr_result zaclr_native_call_frame_set_object(struct zaclr_native_call_frame* frame,
                                                                   zaclr_object_handle value)
{
    if (frame == NULL)
    {
        return invalid_arg();
    }

    frame->has_result = 1u;
    frame->result_value = {};
    frame->result_value.kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
    frame->result_value.data.object_handle = value;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_native_call_frame_set_string(struct zaclr_native_call_frame* frame,
                                                                   zaclr_object_handle value)
{
    return zaclr_native_call_frame_set_object(frame, value);
}

extern "C" struct zaclr_result zaclr_build_native_call_frame(struct zaclr_runtime* runtime,
                                                              struct zaclr_frame* caller_frame,
                                                              const struct zaclr_loaded_assembly* assembly,
                                                              const struct zaclr_method_desc* method,
                                                              uint8_t invocation_kind,
                                                              uint8_t has_this,
                                                              struct zaclr_stack_value* this_value,
                                                              uint8_t argument_count,
                                                              struct zaclr_stack_value* arguments,
                                                              struct zaclr_native_call_frame* frame)
{
    if (runtime == NULL || frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    *frame = {};
    frame->runtime = runtime;
    frame->caller_frame = caller_frame;
    frame->assembly = assembly;
    frame->method = method;
    frame->managed_signature = method != NULL ? &method->signature : NULL;
    frame->invocation_kind = invocation_kind;
    frame->has_this = has_this;
    frame->this_value = this_value;
    frame->argument_count = argument_count;
    frame->arguments = arguments;
    frame->has_result = 0u;
    frame->result_value = {};
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_invoke_internal_call(struct zaclr_native_call_frame* frame,
                                                           const struct zaclr_native_bind_method* method)
{
    if (frame == NULL || method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    if (frame->runtime != NULL)
    {
        ZACLR_TRACE_VALUE(frame->runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          method->method_name,
                          0u);
    }

    if (method->handler == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    frame->has_result = 0u;
    frame->result_value = {};
    return method->handler(*frame);
}
