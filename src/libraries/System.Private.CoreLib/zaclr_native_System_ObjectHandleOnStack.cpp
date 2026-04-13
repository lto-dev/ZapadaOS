#include "zaclr_native_System_ObjectHandleOnStack.h"

struct zaclr_result zaclr_native_System_ObjectHandleOnStack::Create___STATIC__VALUETYPE__BYREF(struct zaclr_native_call_frame& frame)
{
    struct zaclr_stack_value* argument = zaclr_native_call_frame_arg(&frame, 0u);
    if (argument == NULL || argument->kind != ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    if (frame.caller_frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    return zaclr_native_call_frame_set_object(&frame, (zaclr_object_handle)(uintptr_t)argument->data.raw);
}
