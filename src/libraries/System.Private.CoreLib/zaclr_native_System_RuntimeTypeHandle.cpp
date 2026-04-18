#include "zaclr_native_System_RuntimeTypeHandle.h"

#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_type_identity.h>

namespace
{
    static struct zaclr_result load_runtime_type_handle_argument(struct zaclr_native_call_frame& frame,
                                                                 uint32_t index,
                                                                 const struct zaclr_runtime_type_desc** out_type,
                                                                 zaclr_object_handle* out_handle)
    {
        struct zaclr_type_identity identity = {};
        struct zaclr_result result;

        if (out_type == NULL || out_handle == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_type = NULL;
        *out_handle = 0u;

        result = zaclr_native_call_frame_arg_object(&frame, index, out_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (*out_handle == 0u)
        {
            return zaclr_result_ok();
        }

        result = zaclr_type_identity_from_runtime_type_handle(frame.runtime, *out_handle, &identity);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
        zaclr_type_identity_reset(&identity);

        *out_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, *out_handle);
        return *out_type != NULL
            ? zaclr_result_ok()
            : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    static struct zaclr_result load_runtime_type_from_i8(struct zaclr_native_call_frame& frame,
                                                         uint32_t index,
                                                         const struct zaclr_runtime_type_desc** out_type,
                                                         zaclr_object_handle* out_handle)
    {
        int64_t raw_handle = 0;
        struct zaclr_result result;

        if (out_type == NULL || out_handle == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_type = NULL;
        *out_handle = 0u;

        result = zaclr_native_call_frame_arg_i8(&frame, index, &raw_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (raw_handle == 0)
        {
            return zaclr_result_ok();
        }

        result = zaclr_runtime_type_find_by_native_handle(frame.runtime,
                                                          (uintptr_t)raw_handle,
                                                          out_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, *out_handle);
        return *out_type != NULL
            ? zaclr_result_ok()
            : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::RuntimeTypeHandle_GetRuntimeTypeFromHandleSlow___STATIC__VOID__I__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_from_i8(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_store_byref_object(&frame, 1u, runtime_type_handle);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::ToIntPtr___STATIC__I__VALUETYPE(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_i8(&frame,
                                          runtime_type != NULL
                                              ? (int64_t)runtime_type->native_type_handle
                                              : 0);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::FromIntPtr___STATIC__VALUETYPE__I(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_from_i8(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_object(&frame, runtime_type_handle);
}
