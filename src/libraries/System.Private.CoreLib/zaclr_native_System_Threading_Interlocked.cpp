#include "zaclr_native_System_Threading_Interlocked.h"

struct zaclr_result zaclr_native_System_Threading_Interlocked::Exchange64___STATIC__I8__BYREF_I8__I8(struct zaclr_native_call_frame& frame)
{
    int64_t current_value;
    int64_t new_value;
    struct zaclr_result result = zaclr_native_call_frame_load_byref_i8(&frame, 0u, &current_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_native_call_frame_arg_i8(&frame, 1u, &new_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_native_call_frame_store_byref_i8(&frame, 0u, new_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_i8(&frame, current_value);
}

struct zaclr_result zaclr_native_System_Threading_Interlocked::ExchangeObject___STATIC__OBJECT__BYREF_OBJECT__OBJECT(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle current_value;
    zaclr_object_handle new_value;
    struct zaclr_result result = zaclr_native_call_frame_load_byref_object(&frame, 0u, &current_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_native_call_frame_arg_object(&frame, 1u, &new_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_native_call_frame_store_byref_object(&frame, 0u, new_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_object(&frame, current_value);
}
