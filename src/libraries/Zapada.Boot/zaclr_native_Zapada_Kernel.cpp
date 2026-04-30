#include "zaclr_native_Zapada_Kernel.h"

#include <kernel/zaclr/host/zaclr_host.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/interop/zaclr_marshalling.h>
#include <kernel/zaclr/runtime/zaclr_boot_shared.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

static uint32_t zaclr_strlen(const char* value)
{
    uint32_t length = 0u;
    if (value == NULL)
    {
        return 0u;
    }

    while (value[length] != '\0')
    {
        ++length;
    }

    return length;
}

struct zaclr_result zaclr_native_Zapada_Kernel::GetTickCount___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    if (frame.runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    return zaclr_native_call_frame_set_i4(&frame,
        (int32_t)(frame.runtime->state.host != NULL && frame.runtime->state.host->monotonic_ticks != NULL
            ? frame.runtime->state.host->monotonic_ticks()
            : 0u));
}

struct zaclr_result zaclr_native_Zapada_Kernel::SetBootPartLba___STATIC__VOID__I4(struct zaclr_native_call_frame& frame)
{
    int32_t value;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &value);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    zaclr_boot_shared_set_boot_part_lba(value);
    return zaclr_native_call_frame_set_void(&frame);
}

struct zaclr_result zaclr_native_Zapada_Kernel::GetBootCommandLine___STATIC__STRING(struct zaclr_native_call_frame& frame)
{
    const char* command_line;
    zaclr_object_handle handle;
    struct zaclr_result status;

    if (frame.runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    command_line = zaclr_boot_shared_get_command_line();
    status = zaclr_string_allocate_ascii_handle(&frame.runtime->heap,
                                                command_line,
                                                zaclr_strlen(command_line),
                                                &handle);
    return status.status == ZACLR_STATUS_OK ? zaclr_native_call_frame_set_string(&frame, handle) : status;
}
