#include <kernel/zaclr/runtime/zaclr_runtime.h>

#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/process/zaclr_process_launch.h>

extern "C" {
#include <kernel/console.h>
#include <kernel/initramfs/ramdisk.h>
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
        ramdisk_file_t file;
        struct zaclr_slice image;
        struct zaclr_loaded_assembly loaded_assembly;
        const struct zaclr_loaded_assembly* assembly;
        struct zaclr_result result;

        if (runtime == NULL || request == NULL || out_assembly == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
        }

        console_write("[ZACLR] Loader lookup: ");
        console_write(request->image_path);
        console_write("\n");

        if (ramdisk_lookup(request->image_path, &file) != RAMDISK_OK)
        {
            console_write("[ZACLR] Loader lookup failed\n");
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_LOADER);
        }

        console_write("[ZACLR] Loader lookup succeeded size=");
        console_write_dec((uint64_t)file.size);
        console_write("\n");

        image.data = file.data;
        image.size = file.size;

        result = zaclr_loader_load_image(&runtime->loader, &image, &loaded_assembly);
        if (result.status != ZACLR_STATUS_OK)
        {
            console_write("[ZACLR] Loader parse failed\n");
            return result;
        }

        console_write("[ZACLR] Loader parsed assembly: ");
        console_write(loaded_assembly.assembly_name.text);
        console_write("\n");

        result = zaclr_assembly_registry_register(&runtime->assemblies, &loaded_assembly);
        if (result.status == ZACLR_STATUS_ALREADY_EXISTS)
        {
            assembly = zaclr_assembly_registry_find_by_name(&runtime->assemblies, loaded_assembly.assembly_name.text);
            zaclr_loader_release_loaded_assembly(&loaded_assembly);
            if (assembly == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
            }

            console_write("[ZACLR] Loader reused registered assembly: ");
            console_write(assembly->assembly_name.text);
            console_write("\n");
            *out_assembly = assembly;
            return zaclr_result_ok();
        }

        if (result.status != ZACLR_STATUS_OK)
        {
            zaclr_loader_release_loaded_assembly(&loaded_assembly);
            console_write("[ZACLR] Loader register failed\n");
            return result;
        }

        assembly = runtime->assemblies.count != 0u ? &runtime->assemblies.entries[runtime->assemblies.count - 1u] : NULL;
        if (assembly == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
        }

        console_write("[ZACLR] Loader registered assembly id=");
        console_write_dec((uint64_t)assembly->id);
        console_write(" name=");
        console_write(assembly->assembly_name.text);
        console_write("\n");
        *out_assembly = assembly;
        return zaclr_result_ok();
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
