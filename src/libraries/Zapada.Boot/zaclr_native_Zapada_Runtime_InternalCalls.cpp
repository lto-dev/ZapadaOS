#include <kernel/zaclr/exec/zaclr_engine.h>
#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/heap/zaclr_array.h>
#include <kernel/zaclr/heap/zaclr_gc.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>
#include <kernel/zaclr/loader/zaclr_binder.h>
#include <kernel/zaclr/loader/zaclr_assembly_source_vfs.h>
#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

#include "zaclr_native_Zapada_Runtime_InternalCalls.h"

extern "C" {
#include <kernel/console.h>
}

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

struct zaclr_result zaclr_native_Zapada_Runtime_InternalCalls::RuntimeTransitionToVfs___STATIC__I4__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* root_string;
    const char* root_path;
    struct zaclr_app_domain* domain;
    struct zaclr_result status;

    if (frame.runtime == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    status = zaclr_native_call_frame_arg_string(&frame, 0u, &root_string);
    if (status.status != ZACLR_STATUS_OK || root_string == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -2);
    }

    root_path = zaclr_string_ascii_chars(root_string);
    if (root_path == NULL || root_path[0] != '/')
    {
        return zaclr_native_call_frame_set_i4(&frame, -3);
    }

    domain = zaclr_runtime_current_domain(frame.runtime);
    if (domain == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -4);
    }

    status = zaclr_assembly_source_vfs_configure(root_path);
    if (status.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_i4(&frame, -5);
    }

    domain->source = zaclr_assembly_source_vfs();

    console_write("[ZACLR] Assembly source transitioned to VFS root=");
    console_write(root_path);
    console_write("\n");

    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_Zapada_Runtime_InternalCalls::RuntimeBindFromSource___STATIC__I4__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* name_string;
    const char* assembly_name;
    const struct zaclr_loaded_assembly* assembly;
    struct zaclr_app_domain* domain;
    struct zaclr_assembly_identity identity;
    struct zaclr_result status;

    if (frame.runtime == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    status = zaclr_native_call_frame_arg_string(&frame, 0u, &name_string);
    if (status.status != ZACLR_STATUS_OK || name_string == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -2);
    }

    assembly_name = zaclr_string_ascii_chars(name_string);
    if (assembly_name == NULL || assembly_name[0] == '\0')
    {
        return zaclr_native_call_frame_set_i4(&frame, -3);
    }

    domain = zaclr_runtime_current_domain(frame.runtime);
    if (domain == NULL || domain->source == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -4);
    }

    identity = {};
    identity.name = assembly_name;
    identity.name_length = zaclr_string_length(name_string);

    status = zaclr_binder_bind(&frame.runtime->loader, domain, &identity, &assembly);
    if (status.status != ZACLR_STATUS_OK || assembly == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -5);
    }

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)assembly->id);
}

struct zaclr_result zaclr_native_Zapada_Runtime_InternalCalls::RuntimeCreateVfsLaunchState___STATIC__I4__STRING__STRING__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* image_path_string;
    const struct zaclr_string_desc* entry_type_string;
    const struct zaclr_string_desc* entry_method_string;
    const char* image_path;
    const char* entry_type;
    const char* entry_method;
    struct zaclr_launch_request request;
    struct zaclr_launch_state launch_state;
    struct zaclr_app_domain* boot_domain;
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

    image_path = zaclr_string_ascii_chars(image_path_string);
    entry_type = zaclr_string_ascii_chars(entry_type_string);
    entry_method = zaclr_string_ascii_chars(entry_method_string);
    if (image_path == NULL || image_path[0] == '\0')
    {
        return zaclr_native_call_frame_set_i4(&frame, -5);
    }

    if (entry_type != NULL && entry_type[0] == '\0')
    {
        entry_type = NULL;
    }

    if (entry_method != NULL && entry_method[0] == '\0')
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

    status = zaclr_process_launch_request_validate(&request);
    if (status.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_i4(&frame, -6);
    }

    boot_domain = zaclr_runtime_current_domain(frame.runtime);
    if (boot_domain == NULL || boot_domain->source != zaclr_assembly_source_vfs())
    {
        return zaclr_native_call_frame_set_i4(&frame, -7);
    }

    status = zaclr_process_manager_create_launch(&frame.runtime->process_manager,
                                                 &request,
                                                 zaclr_assembly_source_vfs(),
                                                 &launch_state);
    if (status.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_i4(&frame, -8);
    }

    if (launch_state.process.id == frame.runtime->state.boot_process_id
        || launch_state.domain.id == frame.runtime->state.boot_domain_id
        || launch_state.domain.source != zaclr_assembly_source_vfs())
    {
        return zaclr_native_call_frame_set_i4(&frame, -9);
    }

    console_write("[ZACLR] VFS launch state created process=");
    console_write_dec((uint64_t)launch_state.process.id);
    console_write(" domain=");
    console_write_dec((uint64_t)launch_state.domain.id);
    console_write(" image=");
    console_write(image_path);
    console_write("\n");

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)launch_state.process.id);
}

