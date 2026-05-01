#include "zaclr_native_Zapada_Runtime_ShellInternalCalls.h"

#include <kernel/zaclr/include/zaclr_public_api.h>
#include <kernel/zaclr/exec/zaclr_interop_dispatch.h>
#include <kernel/zaclr/heap/zaclr_string.h>

extern "C" {
#include <kernel/console.h>
}

struct zaclr_result zaclr_native_Zapada_Runtime_ShellInternalCalls::RuntimeLaunchTask___STATIC__I4__STRING__STRING__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* image_path_string;
    const struct zaclr_string_desc* entry_type_string;
    const struct zaclr_string_desc* entry_method_string;
    const char* image_path;
    const char* entry_type;
    const char* entry_method;
    struct zaclr_launch_request request;
    zaclr_process_id process_id = 0u;
    struct zaclr_result status;

    if (frame.runtime == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    status = zaclr_native_call_frame_arg_string(&frame, 0u, &image_path_string);
    if (status.status != ZACLR_STATUS_OK || image_path_string == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -2);
    }

    status = zaclr_native_call_frame_arg_string(&frame, 1u, &entry_type_string);
    if (status.status != ZACLR_STATUS_OK || entry_type_string == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -3);
    }

    status = zaclr_native_call_frame_arg_string(&frame, 2u, &entry_method_string);
    if (status.status != ZACLR_STATUS_OK || entry_method_string == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -4);
    }

    /* Convert UTF-16 strings to ASCII. Managed strings from Substring() don't
       have an ASCII shadow, so we must convert from UTF-16 code units. */
    static char image_buf[256];
    static char type_buf[256];
    static char method_buf[256];

    {
        const uint16_t* chars = zaclr_string_chars(image_path_string);
        uint32_t len = zaclr_string_length(image_path_string);
        if (len > 255u) len = 255u;
        for (uint32_t i = 0u; i < len; ++i)
            image_buf[i] = (char)(chars[i] & 0x7F);
        image_buf[len] = '\0';
        image_path = image_buf;
    }

    {
        const uint16_t* chars = zaclr_string_chars(entry_type_string);
        uint32_t len = zaclr_string_length(entry_type_string);
        if (len > 255u) len = 255u;
        for (uint32_t i = 0u; i < len; ++i)
            type_buf[i] = (char)(chars[i] & 0x7F);
        type_buf[len] = '\0';
        entry_type = type_buf;
    }

    {
        const uint16_t* chars = zaclr_string_chars(entry_method_string);
        uint32_t len = zaclr_string_length(entry_method_string);
        if (len > 255u) len = 255u;
        for (uint32_t i = 0u; i < len; ++i)
            method_buf[i] = (char)(chars[i] & 0x7F);
        method_buf[len] = '\0';
        entry_method = method_buf;
    }

    if (image_path[0] == '\0')
    {
        return zaclr_native_call_frame_set_i4(&frame, -5);
    }

    if (entry_type[0] == '\0')
    {
        entry_type = NULL;
    }

    if (entry_method[0] == '\0')
    {
        entry_method = NULL;
    }

    request = {};
    request.image_path = image_path;
    request.entry_type = entry_type;
    request.entry_method = entry_method;
    request.user = 0u;
    request.group = 0u;
    request.flags = 0u;

    status = zaclr_runtime_launch_task(frame.runtime, &request, &process_id);
    if (status.status != ZACLR_STATUS_OK)
    {
        console_write("[Shell][run] task launch failed status=");
        console_write_dec((uint64_t)status.status);
        console_write(" category=");
        console_write_dec((uint64_t)status.category);
        console_write("\n");
        return zaclr_native_call_frame_set_i4(&frame, -10);
    }

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)process_id);
}
