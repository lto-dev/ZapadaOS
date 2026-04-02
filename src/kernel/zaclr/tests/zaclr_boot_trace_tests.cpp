#include <kernel/zaclr/include/zaclr_trace.h>

#include <kernel/zaclr/heap/zaclr_gc_roots.h>
#include <kernel/zaclr/process/zaclr_handle_table.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

namespace
{
    struct root_counter_context {
        uint32_t count;
    };

    static void count_root(zaclr_object_handle*, uint32_t, void* context)
    {
        struct root_counter_context* counter = (struct root_counter_context*)context;
        if (counter != NULL)
        {
            counter->count++;
        }
    }
}

extern "C" struct zaclr_result zaclr_run_boot_trace_tests(void)
{
    struct zaclr_runtime runtime = {};
    struct zaclr_stack_value static_root = {};
    struct zaclr_loaded_assembly assembly = {};
    zaclr_object_handle handle_root = 2u;
    struct root_counter_context counter = {};
    struct zaclr_gc_root_visitor visitor = {};
    struct zaclr_result result;

    result = zaclr_handle_table_initialize(&runtime.boot_launch.handle_table, 1u, 1u);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_handle_table_store(&runtime.boot_launch.handle_table, handle_root, &counter.count);
    if (result.status != ZACLR_STATUS_OK)
    {
        zaclr_handle_table_reset(&runtime.boot_launch.handle_table);
        return result;
    }

    counter.count = 0u;

    static_root.kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
    static_root.data.object_handle = 1u;
    assembly.static_fields = &static_root;
    assembly.static_field_count = 1u;
    runtime.assemblies.entries = &assembly;
    runtime.assemblies.count = 1u;
    runtime.assemblies.capacity = 1u;

    visitor.visit_handle = count_root;
    visitor.context = &counter;
    zaclr_gc_enumerate_runtime_roots(&runtime, &visitor);

    result = counter.count == 2u
        ? zaclr_result_ok()
        : zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_DIAG);
    zaclr_handle_table_reset(&runtime.boot_launch.handle_table);
    return result;
}
