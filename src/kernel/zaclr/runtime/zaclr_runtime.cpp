#include <kernel/zaclr/runtime/zaclr_runtime.h>

#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/process/zaclr_handle_table.h>

extern "C" void zaclr_runtime_reset(struct zaclr_runtime* runtime)
{
    if (runtime == NULL)
    {
        return;
    }

    zaclr_heap_reset(&runtime->heap);
    zaclr_handle_table_reset(&runtime->finalizer_queue);
    zaclr_handle_table_reset(&runtime->boot_launch.handle_table);

    runtime->state.host = NULL;
    runtime->state.config.trace = zaclr_trace_config_default();
    runtime->state.config.enable_metadata_validation = false;
    runtime->state.config.enable_opcode_trace = false;
    runtime->state.config.enable_heap_trace = false;
    runtime->state.boot_process_id = 0u;
    runtime->state.boot_thread_id = 0u;
    runtime->state.boot_domain_id = 0u;
    runtime->state.boot_assembly_id = 0u;
    runtime->state.boot_entry_method_id = 0u;
    runtime->state.boot_completed_method_id = 0u;
    runtime->state.flags = 0u;
    runtime->process_manager = {};
    runtime->loader = {};
    runtime->assemblies = {};
    runtime->internal_calls = {};
    runtime->qcall_table = {};
    runtime->heap = {};
    runtime->engine = {};
    runtime->boot_launch = {};
}

extern "C" struct zaclr_app_domain* zaclr_runtime_current_domain(struct zaclr_runtime* runtime)
{
    if (runtime == NULL)
    {
        return NULL;
    }

    return &runtime->boot_launch.domain;
}
