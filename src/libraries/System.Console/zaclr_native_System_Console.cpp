#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>
#include <kernel/zaclr/interop/zaclr_marshalling.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

#include "zaclr_native_System_Console.h"

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static struct zaclr_result zaclr_native_console_write_common(struct zaclr_native_call_frame& frame,
                                                                 bool append_newline)
    {
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

        if (string_handle != 0u)
        {
            const char* text = zaclr_string_chars_from_handle(&frame.runtime->heap, string_handle);
            if (text == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
            }

            console_write(text);
        }

        if (append_newline)
        {
            console_write("\n");
        }

        return zaclr_native_call_frame_set_void(&frame);
    }
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__STRING(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_console_write_common(frame, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__I4(struct zaclr_native_call_frame& frame)
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

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__STRING(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_console_write_common(frame, true);
}
