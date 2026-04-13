#include "zaclr_native_System_Type.h"

#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_type_identity.h>

namespace
{
    static struct zaclr_result get_runtime_type_from_this(struct zaclr_native_call_frame& frame,
                                                          zaclr_object_handle* out_handle,
                                                          const struct zaclr_runtime_type_desc** out_type)
    {
        struct zaclr_type_identity identity = {};
        struct zaclr_result result;

        if (frame.this_value == NULL || out_handle == NULL || out_type == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if (frame.this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_handle = zaclr_heap_get_object_handle(&frame.runtime->heap, frame.this_value->data.object_reference);
        if (*out_handle == 0u)
        {
            *out_type = NULL;
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        result = zaclr_type_identity_from_runtime_type_handle(frame.runtime, *out_handle, &identity);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
        zaclr_type_identity_reset(&identity);

        *out_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, *out_handle);
        if (*out_type == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        return zaclr_result_ok();
    }

    static struct zaclr_result get_runtime_type_from_i8(struct zaclr_native_call_frame& frame,
                                                        uint32_t index,
                                                        zaclr_object_handle* out_handle)
    {
        int64_t raw_handle = 0;
        struct zaclr_type_identity identity = {};
        struct zaclr_result result = zaclr_native_call_frame_arg_i8(&frame, index, &raw_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_handle = (zaclr_object_handle)(uintptr_t)raw_handle;
        if (*out_handle == 0u)
        {
            return zaclr_result_ok();
        }

        result = zaclr_type_identity_from_runtime_type_handle(frame.runtime, *out_handle, &identity);
        if (result.status == ZACLR_STATUS_OK)
        {
            zaclr_type_identity_reset(&identity);
        }

        return result;
    }
}

struct zaclr_result zaclr_native_System_Type::GetTypeFromHandle___STATIC__CLASS__VALUETYPE(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle handle = 0u;
    struct zaclr_result result = zaclr_native_call_frame_arg_object(&frame, 0u, &handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (handle != 0u && zaclr_runtime_type_from_handle_const(&frame.runtime->heap, handle) == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    return zaclr_native_call_frame_set_object(&frame, handle);
}

struct zaclr_result zaclr_native_System_Type::get_TypeHandle___INSTANCE__VALUETYPE(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = get_runtime_type_from_this(frame, &handle, &runtime_type);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)runtime_type;
    return zaclr_native_call_frame_set_object(&frame, handle);
}

struct zaclr_result zaclr_native_System_Type::GetTypeFromHandleUnsafe___STATIC__CLASS__I(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle handle = 0u;
    struct zaclr_result result = get_runtime_type_from_i8(frame, 0u, &handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_object(&frame, handle);
}
