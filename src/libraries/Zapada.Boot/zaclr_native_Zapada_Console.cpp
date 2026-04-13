#include "zaclr_native_Zapada_Console.h"

#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/interop/zaclr_marshalling.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

extern "C" {
#include <kernel/console.h>
}

struct zaclr_result zaclr_native_Zapada_Console::Write___STATIC__VOID__STRING(struct zaclr_native_call_frame& frame)
{
    const char* text;
    zaclr_object_handle string_handle;
    struct zaclr_result status;

    if (frame.runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    status = zaclr_native_call_frame_arg_object(&frame, 0u, &string_handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    if (string_handle == 0u)
    {
        return zaclr_native_call_frame_set_void(&frame);
    }

    text = zaclr_string_ascii_chars_from_handle(&frame.runtime->heap, string_handle);
    if (text == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    console_write(text);

    return zaclr_native_call_frame_set_void(&frame);
}

struct zaclr_result zaclr_native_Zapada_Console::WriteInt___STATIC__VOID__I4(struct zaclr_native_call_frame& frame)
{
    int32_t value;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &value);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    console_write_dec((uint64_t)(uint32_t)value);
    return zaclr_native_call_frame_set_void(&frame);
}

struct zaclr_result zaclr_native_Zapada_Console::WriteHex___STATIC__VOID__I4(struct zaclr_native_call_frame& frame)
{
    int32_t value;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &value);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    console_write_hex64((uint64_t)(uint32_t)value);
    return zaclr_native_call_frame_set_void(&frame);
}
