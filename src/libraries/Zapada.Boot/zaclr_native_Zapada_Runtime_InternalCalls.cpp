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

    /* Mark as ZOMBIE: this was a validation-only probe, not a real process. */
    zaclr_process_table_set_state(&frame.runtime->process_manager.table,
                                   launch_state.process.id,
                                   ZACLR_PROCESS_STATE_ZOMBIE);

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)launch_state.process.id);
}

struct zaclr_result zaclr_native_Zapada_Runtime_InternalCalls::RuntimeLaunchTask___STATIC__I4__STRING__STRING__STRING(struct zaclr_native_call_frame& frame)
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

    status = zaclr_runtime_launch_task(frame.runtime, &request, &process_id);
    if (status.status != ZACLR_STATUS_OK)
    {
        console_write("[ZACLR][ic] RuntimeLaunchTask failed status=");
        console_write_dec((uint64_t)status.status);
        console_write(" category=");
        console_write_dec((uint64_t)status.category);
        console_write("\n");
        return zaclr_native_call_frame_set_i4(&frame, -10);
    }

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)process_id);
}

struct zaclr_result zaclr_native_Zapada_Runtime_InternalCalls::RuntimeGetProcessCount___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    if (frame.runtime == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, 0);
    }

    uint32_t count = zaclr_process_table_count(&frame.runtime->process_manager.table);
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)count);
}

struct zaclr_result zaclr_native_Zapada_Runtime_InternalCalls::RuntimeGetProcessInfo___STATIC__STRING__I4(struct zaclr_native_call_frame& frame)
{
    int32_t index;
    struct zaclr_result status;

    if (frame.runtime == NULL)
    {
        return zaclr_native_call_frame_set_string(&frame, 0u);
    }

    status = zaclr_native_call_frame_arg_i4(&frame, 0u, &index);
    if (status.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_string(&frame, 0u);
    }

    if (index < 0 || (uint32_t)index >= ZACLR_PROCESS_TABLE_MAX_ENTRIES)
    {
        return zaclr_native_call_frame_set_string(&frame, 0u);
    }

    const struct zaclr_process_entry* entry = &frame.runtime->process_manager.table.entries[(uint32_t)index];
    if (entry->state == ZACLR_PROCESS_STATE_FREE || entry->state == ZACLR_PROCESS_STATE_ZOMBIE)
    {
        return zaclr_native_call_frame_set_string(&frame, 0u);
    }

    /* Format: "PID  PPID STATE    DOMAIN IMAGE" matching the header in BuildProcesses(). */
    char buf[256];
    int pos = 0;

    /* Helper macro: write a uint32 right-justified in a fixed field. */
    #define WRITE_UINT_FIELD(val, width) do { \
        uint32_t _v = (val); \
        char _d[12]; int _dc = 0; \
        if (_v == 0u) { _d[_dc++] = '0'; } \
        else { while (_v > 0u) { _d[_dc++] = (char)('0' + (_v % 10u)); _v /= 10u; } } \
        int _pad = (width) - _dc; \
        while (_pad-- > 0 && pos < 250) { buf[pos++] = ' '; } \
        for (int _i = _dc - 1; _i >= 0 && pos < 250; --_i) { buf[pos++] = _d[_i]; } \
    } while (0)

    #define WRITE_STR_FIELD(str, width) do { \
        const char* _s = (str); \
        int _len = 0; \
        while (_s[_len] != '\0') { _len++; } \
        int _i2 = 0; \
        while (_i2 < _len && pos < 250) { buf[pos++] = _s[_i2++]; } \
        int _pad = (width) - _len; \
        while (_pad-- > 0 && pos < 250) { buf[pos++] = ' '; } \
    } while (0)

    /* PID (4 chars) */
    WRITE_UINT_FIELD(entry->pid, 4u);
    buf[pos++] = ' ';
    /* PPID (4 chars) */
    WRITE_UINT_FIELD(entry->ppid, 4u);
    buf[pos++] = ' ';
    /* STATE (8 chars left-aligned) */
    WRITE_STR_FIELD(zaclr_process_state_name(entry->state), 8u);
    buf[pos++] = ' ';
    /* DOMAIN (4 chars) */
    WRITE_UINT_FIELD(entry->domain_id, 4u);
    buf[pos++] = ' ';
    /* IMAGE (variable) */
    {
        const char* img = entry->image_name;
        while (*img != '\0' && pos < 250) { buf[pos++] = *img++; }
    }

    #undef WRITE_UINT_FIELD
    #undef WRITE_STR_FIELD

    buf[pos] = '\0';

    zaclr_object_handle string_handle = 0u;
    status = zaclr_string_allocate_ascii_handle(&frame.runtime->heap, buf, (uint32_t)pos, &string_handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_string(&frame, 0u);
    }

    return zaclr_native_call_frame_set_string(&frame, string_handle);
}

