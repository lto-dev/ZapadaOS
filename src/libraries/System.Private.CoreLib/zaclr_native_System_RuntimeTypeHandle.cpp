#include "zaclr_native_System_RuntimeTypeHandle.h"

#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_type_identity.h>

namespace
{
    static struct zaclr_result load_runtime_type_handle_argument(struct zaclr_native_call_frame& frame,
                                                                 uint32_t index,
                                                                 zaclr_object_handle* out_handle)
    {
        struct zaclr_type_identity identity = {};
        struct zaclr_result result = zaclr_native_call_frame_arg_object(&frame, index, out_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

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

    static struct zaclr_result load_runtime_type_from_i8(struct zaclr_native_call_frame& frame,
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

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::RuntimeTypeHandle_GetRuntimeTypeFromHandleSlow___STATIC__VOID__I__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    struct zaclr_result result = load_runtime_type_from_i8(frame, 0u, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_store_byref_object(&frame, 1u, runtime_type_handle);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::ToIntPtr___STATIC__I__VALUETYPE(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_i8(&frame, (int64_t)(uintptr_t)runtime_type_handle);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::FromIntPtr___STATIC__VALUETYPE__I(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    struct zaclr_result result = load_runtime_type_from_i8(frame, 0u, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_object(&frame, runtime_type_handle);
}
