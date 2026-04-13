#include <kernel/zaclr/runtime/zaclr_runtime.h>

#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/loader/zaclr_binder.h>
#include <kernel/zaclr/process/zaclr_process_launch.h>

#include "../../../libraries/System.Private.CoreLib/zaclr_native_System_GC.h"
#include "../../../libraries/System.Private.CoreLib/zaclr_native_System_Runtime_InteropServices_GCHandle.h"
#include "../../../libraries/System.Private.CoreLib/zaclr_native_System_RuntimeHelpers.h"

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static void zaclr_runtime_write_label_value(const char* label, uint64_t value)
    {
        console_write(label);
        console_write(" = 0x");
        console_write_hex64(value);
        console_write(" (");
        console_write_dec(value);
        console_write(")\n");
    }

    static struct zaclr_result zaclr_runtime_load_launch_image(struct zaclr_runtime* runtime,
                                                               const struct zaclr_launch_request* request,
                                                               const struct zaclr_loaded_assembly** out_assembly)
    {
        struct zaclr_assembly_identity identity = {};
        struct zaclr_app_domain* domain;
        size_t name_length = 0u;

        if (runtime == NULL || request == NULL || out_assembly == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
        }

        domain = zaclr_runtime_current_domain(runtime);
        if (domain == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
        }

        console_write("[ZACLR] Loader lookup: ");
        console_write(request->image_path);
        console_write("\n");

        while (request->image_path[name_length] != '\0')
        {
            ++name_length;
        }

        if (name_length > 4u
            && request->image_path[name_length - 4u] == '.'
            && request->image_path[name_length - 3u] == 'd'
            && request->image_path[name_length - 2u] == 'l'
            && request->image_path[name_length - 1u] == 'l')
        {
            name_length -= 4u;
        }

        if (name_length == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
        }

        identity.name = request->image_path;
        identity.name_length = (uint32_t)name_length;
        return zaclr_binder_bind(&runtime->loader, domain, &identity, out_assembly);
    }
}

extern "C" struct zaclr_result zaclr_runtime_initialize(struct zaclr_runtime* runtime,
                                                         const struct zaclr_bootstrap_contract* bootstrap,
                                                         const struct zaclr_runtime_config* config)
{
    struct zaclr_result result;

    if (runtime == NULL || bootstrap == NULL || config == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_RUNTIME);
    }

    zaclr_runtime_reset(runtime);
    runtime->state.host = bootstrap->host;
    runtime->state.config = *config;

    ZACLR_TRACE_TEXT(runtime, ZACLR_TRACE_CATEGORY_RUNTIME, ZACLR_TRACE_EVENT_RUNTIME_INIT_BEGIN, "zaclr_runtime_initialize");

    result = zaclr_process_manager_initialize(&runtime->process_manager);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_loader_initialize(&runtime->loader);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_assembly_registry_initialize(&runtime->assemblies);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_internal_call_registry_initialize(&runtime->internal_calls);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_qcall_table_initialize(&runtime->qcall_table);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_qcall_table_register(&runtime->qcall_table,
                                        "GCInterface_Collect",
                                        zaclr_native_System_GC::_Collect___STATIC__VOID__I4__I4__BOOLEAN);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_qcall_table_register(&runtime->qcall_table,
                                        "GCInterface_WaitForPendingFinalizers",
                                        zaclr_native_System_GC::WaitForPendingFinalizers___STATIC__VOID);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_qcall_table_register(&runtime->qcall_table,
                                        "GCInterface_ReRegisterForFinalize",
                                        zaclr_native_System_GC::GCInterface_ReRegisterForFinalize___STATIC__VOID__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_qcall_table_register(&runtime->qcall_table,
                                        "GCHandle_InternalFreeWithGCTransition",
                                        zaclr_native_System_Runtime_InteropServices_GCHandle::GCHandle_InternalFreeWithGCTransition___STATIC__VOID__I);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_qcall_table_register(&runtime->qcall_table,
                                        "ReflectionInvocation_RunClassConstructor",
                                        zaclr_native_System_RuntimeHelpers::ReflectionInvocation_RunClassConstructor___STATIC__VOID__VALUETYPE);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_heap_initialize(&runtime->heap, runtime);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_handle_table_initialize(&runtime->finalizer_queue, 1u, 64u);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_internal_call_registry_register_generated(&runtime->internal_calls);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_engine_initialize(&runtime->engine);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    ZACLR_TRACE_VALUE(runtime,
                      ZACLR_TRACE_CATEGORY_RUNTIME,
                      ZACLR_TRACE_EVENT_RUNTIME_INIT_END,
                      "zaclr_runtime_initialize.internal_calls",
                      (uint64_t)runtime->internal_calls.assembly_count);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_runtime_shutdown(struct zaclr_runtime*)
{
    return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_RUNTIME);
}

extern "C" struct zaclr_result zaclr_runtime_launch(struct zaclr_runtime* runtime,
                                                     const struct zaclr_launch_request* request,
                                                     zaclr_process_id* out_process_id)
{
    const struct zaclr_loaded_assembly* assembly;
    const struct zaclr_method_desc* entry_method;
    struct zaclr_result result;

    if (out_process_id != NULL)
    {
        *out_process_id = 0u;
    }

    if (runtime == NULL || request == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    result = zaclr_process_launch_request_validate(request);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_process_manager_create_boot_launch(&runtime->process_manager, request, &runtime->boot_launch);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    console_write("[ZACLR] Launch request image=");
    console_write(request->image_path);
    console_write(" type=");
    console_write(request->entry_type != NULL ? request->entry_type : "<entrypoint>");
    console_write(" method=");
    console_write(request->entry_method != NULL ? request->entry_method : "<entrypoint>");
    console_write("\n");

    {
        struct zaclr_assembly_identity corelib_identity = {};
        const struct zaclr_loaded_assembly* corelib_assembly = NULL;

        corelib_identity.name = "System.Private.CoreLib";
        corelib_identity.name_length = 22u;
        result = zaclr_binder_bind(&runtime->loader,
                                   &runtime->boot_launch.domain,
                                   &corelib_identity,
                                   &corelib_assembly);
        if (result.status != ZACLR_STATUS_OK)
        {
            console_write("[ZACLR] CoreLib preload failed\n");
            return result;
        }
    }

    result = zaclr_runtime_load_launch_image(runtime, request, &assembly);
    if (result.status != ZACLR_STATUS_OK)
    {
        console_write("[ZACLR] Launch image load failed\n");
        return result;
    }

    ZACLR_TRACE_VALUE(runtime,
                      ZACLR_TRACE_CATEGORY_LOADER,
                      ZACLR_TRACE_EVENT_IMAGE_REGISTER,
                      request->image_path,
                      (uint64_t)assembly->id);

    console_write("[ZACLR] About to resolve launch entry point assembly=");
    console_write(assembly->assembly_name.text != NULL ? assembly->assembly_name.text : "<null>");
    console_write(" type_count=");
    console_write_dec((uint64_t)assembly->type_map.count);
    console_write(" method_count=");
    console_write_dec((uint64_t)assembly->method_map.count);
    console_write("\n");

    result = zaclr_process_resolve_launch_entry_point(assembly,
                                                      request,
                                                      &runtime->boot_launch.entry_point,
                                                      &entry_method);
    if (result.status != ZACLR_STATUS_OK)
    {
        console_write("[ZACLR] Entry-point bind failed\n");
        return result;
    }

    console_write("[ZACLR] Entry-point bound assembly=");
    console_write(assembly->assembly_name.text);
    console_write(" method=");
    console_write(entry_method->name.text);
    console_write("\n");

    runtime->boot_launch.assembly = assembly;
    runtime->boot_launch.entry_method = entry_method;
    runtime->state.boot_process_id = runtime->boot_launch.process.id;
    runtime->state.boot_thread_id = runtime->boot_launch.thread.id;
    runtime->state.boot_domain_id = runtime->boot_launch.domain.id;
    runtime->state.boot_assembly_id = assembly->id;
    runtime->state.boot_entry_method_id = entry_method->id;

    ZACLR_TRACE_VALUE(runtime,
                      ZACLR_TRACE_CATEGORY_PROCESS,
                      ZACLR_TRACE_EVENT_PROCESS_CREATE,
                      request->image_path,
                      (uint64_t)runtime->boot_launch.process.id);
    ZACLR_TRACE_VALUE(runtime,
                      ZACLR_TRACE_CATEGORY_PROCESS,
                      ZACLR_TRACE_EVENT_PROCESS_LAUNCH,
                      entry_method->name.text,
                      (uint64_t)entry_method->id);

    zaclr_runtime_write_label_value("[ZACLR] Bound assembly id", (uint64_t)assembly->id);
    zaclr_runtime_write_label_value("[ZACLR] Bound method id", (uint64_t)entry_method->id);

    if (out_process_id != NULL)
    {
        *out_process_id = runtime->boot_launch.process.id;
    }

    console_write("[ZACLR] Launch state summary process=");
    console_write_dec((uint64_t)runtime->boot_launch.process.id);
    console_write(" thread=");
    console_write_dec((uint64_t)runtime->boot_launch.thread.id);
    console_write(" domain=");
    console_write_dec((uint64_t)runtime->boot_launch.domain.id);
    console_write(" entry_method_id=");
    console_write_dec((uint64_t)entry_method->id);
    console_write("\n");

    runtime->boot_launch.assembly = zaclr_assembly_registry_find_by_id(&runtime->boot_launch.domain.registry,
                                                                       runtime->state.boot_assembly_id);
    if (runtime->boot_launch.assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    result = zaclr_process_resolve_launch_entry_point(runtime->boot_launch.assembly,
                                                      request,
                                                      &runtime->boot_launch.entry_point,
                                                      &runtime->boot_launch.entry_method);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    console_write("[ZACLR] Launch assembly ptr=");
    console_write_hex64((uint64_t)(uintptr_t)runtime->boot_launch.assembly);
    console_write(" name_ptr=");
    console_write_hex64((uint64_t)(uintptr_t)(runtime->boot_launch.assembly->assembly_name.text));
    console_write(" method_ptr=");
    console_write_hex64((uint64_t)(uintptr_t)runtime->boot_launch.entry_method);
    console_write(" method_name_ptr=");
    console_write_hex64((uint64_t)(uintptr_t)(runtime->boot_launch.entry_method != NULL ? runtime->boot_launch.entry_method->name.text : NULL));
    console_write("\n");

    console_write("[ZACLR] Starting launch execution\n");
    result = zaclr_engine_execute_launch(&runtime->engine, runtime, &runtime->boot_launch);
    if (result.status != ZACLR_STATUS_OK)
    {
        console_write("[ZACLR] Launch execution stopped before completion\n");
        return result;
    }

    console_write("[ZACLR] Launch execution completed\n");
    return zaclr_result_ok();
}
