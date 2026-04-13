#include "zaclr_native_System_Threading_Monitor.h"

namespace
{
    static struct zaclr_result validate_object_argument(struct zaclr_native_call_frame& frame,
                                                        uint32_t index)
    {
        zaclr_object_handle handle = 0u;
        struct zaclr_result result = zaclr_native_call_frame_arg_object(&frame, index, &handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return handle == 0u
            ? zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP)
            : zaclr_result_ok();
    }
}

struct zaclr_result zaclr_native_System_Threading_Monitor::TryEnter_FastPath___STATIC__BOOLEAN__OBJECT(struct zaclr_native_call_frame& frame)
{
    struct zaclr_result result = validate_object_argument(frame, 0u);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_bool(&frame, true);
}

struct zaclr_result zaclr_native_System_Threading_Monitor::TryEnter_FastPath_WithTimeout___STATIC__I4__OBJECT__I4(struct zaclr_native_call_frame& frame)
{
    int32_t milliseconds_timeout;
    struct zaclr_result result = validate_object_argument(frame, 0u);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_native_call_frame_arg_i4(&frame, 1u, &milliseconds_timeout);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)milliseconds_timeout;
    return zaclr_native_call_frame_set_i4(&frame, 1);
}

struct zaclr_result zaclr_native_System_Threading_Monitor::Enter_Slowpath___STATIC__VOID__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack(struct zaclr_native_call_frame& frame)
{
    (void)frame;
    return zaclr_native_call_frame_set_void(&frame);
}

struct zaclr_result zaclr_native_System_Threading_Monitor::TryEnter_Slowpath___STATIC__I4__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack__I4(struct zaclr_native_call_frame& frame)
{
    int32_t milliseconds_timeout;
    struct zaclr_result result = zaclr_native_call_frame_arg_i4(&frame, 1u, &milliseconds_timeout);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)milliseconds_timeout;
    return zaclr_native_call_frame_set_i4(&frame, 1);
}
