#include <kernel/zaclr/exec/zaclr_engine.h>
#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/heap/zaclr_array.h>
#include <kernel/zaclr/heap/zaclr_gc.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>
#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

#include "zaclr_native_Zapada_Runtime_InternalCalls.h"

struct zaclr_result zaclr_native_Zapada_Runtime_InternalCalls::RuntimeLoad___STATIC__I4__SZARRAY_U1(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_array_desc* dll_bytes;
    struct zaclr_slice image;
    struct zaclr_loaded_assembly loaded_assembly;
    struct zaclr_result status;

    if (frame.runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    status = zaclr_native_call_frame_arg_array(&frame, 0u, &dll_bytes);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    if (dll_bytes == NULL || zaclr_array_element_size(dll_bytes) != 1u)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    image.data = (const uint8_t*)zaclr_array_data_const(dll_bytes);
    image.size = zaclr_array_length(dll_bytes);
    status = zaclr_loader_load_image(&frame.runtime->loader, &image, &loaded_assembly);
    if (status.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    status = zaclr_assembly_registry_register(&frame.runtime->assemblies, &loaded_assembly);
    if (status.status == ZACLR_STATUS_ALREADY_EXISTS)
    {
        const struct zaclr_loaded_assembly* assembly = zaclr_assembly_registry_find_by_name(&frame.runtime->assemblies,
                                                                                            loaded_assembly.assembly_name.text);
        zaclr_loader_release_loaded_assembly(&loaded_assembly);
        return zaclr_native_call_frame_set_i4(&frame, assembly != NULL ? (int32_t)assembly->id : -1);
    }

    if (status.status != ZACLR_STATUS_OK)
    {
        zaclr_loader_release_loaded_assembly(&loaded_assembly);
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)loaded_assembly.id);
}

