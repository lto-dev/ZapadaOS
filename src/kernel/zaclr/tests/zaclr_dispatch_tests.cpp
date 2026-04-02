#include <kernel/zaclr/exec/zaclr_dispatch.h>

#include <kernel/zaclr/heap/zaclr_gc.h>
#include <kernel/zaclr/heap/zaclr_gc_roots.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/process/zaclr_handle_table.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

extern "C" {
#include <kernel/support/kernel_memory.h>
}

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

    static struct zaclr_result expect(bool condition)
    {
        return condition ? zaclr_result_ok()
                         : zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_EXEC);
    }

    static struct zaclr_result run_heap_retry_smoke_test(void)
    {
        struct zaclr_runtime runtime = {};
        struct zaclr_result result = zaclr_heap_initialize(&runtime.heap, &runtime);
        struct zaclr_object_desc* object = NULL;

        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        runtime.heap.collection_threshold_bytes = 1u;
        result = zaclr_heap_allocate_object(&runtime.heap,
                                            sizeof(struct zaclr_object_desc),
                                            NULL,
                                            0u,
                                            ZACLR_OBJECT_FAMILY_INSTANCE,
                                            ZACLR_OBJECT_FLAG_REFERENCE_TYPE,
                                            &object);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_gc_collect(&runtime);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return expect(zaclr_gc_collection_count(&runtime) != 0u);
    }

    static struct zaclr_result run_reference_object_graph_test(void)
    {
        struct zaclr_runtime runtime = {};
        zaclr_object_handle parent_handle = 0u;
        zaclr_object_handle child_handle = 0u;
        struct zaclr_reference_object_desc* parent;
        struct zaclr_stack_value field_value = {};
        struct zaclr_result result = zaclr_heap_initialize(&runtime.heap, &runtime);

        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 1u, zaclr_token_make(0u), 1u, &parent_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 2u, zaclr_token_make(0u), 0u, &child_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        parent = zaclr_reference_object_from_handle(&runtime.heap, parent_handle);
        if (parent == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_EXEC);
        }

        field_value.kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        field_value.data.object_handle = child_handle;
        result = zaclr_reference_object_store_field(parent,
                                                    zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | 1u),
                                                    &field_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        runtime.boot_launch.thread.current_exception = parent_handle;
        result = zaclr_gc_collect(&runtime);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return expect(zaclr_heap_get_object(&runtime.heap, child_handle) != NULL);
    }

    static struct zaclr_result run_unrooted_object_collection_test(void)
    {
        struct zaclr_runtime runtime = {};
        zaclr_object_handle handle = 0u;
        struct zaclr_result result = zaclr_heap_initialize(&runtime.heap, &runtime);

        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 1u, zaclr_token_make(0u), 0u, &handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_gc_collect(&runtime);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (zaclr_heap_get_object(&runtime.heap, handle) != NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_EXEC);
        }

        return expect(zaclr_heap_live_object_count(&runtime.heap) == 0u);
    }

    static struct zaclr_result run_static_root_survival_test(void)
    {
        struct zaclr_runtime runtime = {};
        struct zaclr_loaded_assembly assembly = {};
        struct zaclr_stack_value* static_fields;
        zaclr_object_handle handle = 0u;
        struct zaclr_result result = zaclr_heap_initialize(&runtime.heap, &runtime);

        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        static_fields = (struct zaclr_stack_value*)kernel_alloc(sizeof(struct zaclr_stack_value));
        if (static_fields == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *static_fields = {};
        assembly.static_fields = static_fields;
        assembly.static_field_count = 1u;

        runtime.assemblies.entries = &assembly;
        runtime.assemblies.count = 1u;
        runtime.assemblies.capacity = 1u;

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 1u, zaclr_token_make(0u), 0u, &handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            kernel_free(static_fields);
            return result;
        }

        static_fields[0].kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        static_fields[0].data.object_handle = handle;

        result = zaclr_gc_collect(&runtime);
        if (result.status != ZACLR_STATUS_OK)
        {
            kernel_free(static_fields);
            return result;
        }

        result = expect(zaclr_heap_get_object(&runtime.heap, handle) != NULL);
        kernel_free(static_fields);
        return result;
    }

    static struct zaclr_result run_boxed_value_survival_test(void)
    {
        struct zaclr_runtime runtime = {};
        zaclr_object_handle boxed_handle = 0u;
        zaclr_object_handle child_handle = 0u;
        struct zaclr_stack_value boxed_value = {};
        struct zaclr_boxed_value_desc* boxed;
        struct zaclr_result result = zaclr_heap_initialize(&runtime.heap, &runtime);

        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 1u, zaclr_token_make(0u), 0u, &child_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        boxed_value.kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        boxed_value.data.object_handle = child_handle;
        result = zaclr_boxed_value_allocate(&runtime.heap,
                                            zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | 1u),
                                            &boxed_value,
                                            &boxed_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        boxed = zaclr_boxed_value_from_handle(&runtime.heap, boxed_handle);
        if (boxed == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_EXEC);
        }

        runtime.boot_launch.thread.current_exception = boxed_handle;
        result = zaclr_gc_collect(&runtime);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return expect(zaclr_heap_get_object(&runtime.heap, child_handle) != NULL);
    }

    static struct zaclr_result run_dynamic_field_capacity_test(void)
    {
        struct zaclr_runtime runtime = {};
        struct zaclr_loaded_assembly assembly = {};
        struct zaclr_type_desc type_desc = {};
        struct zaclr_type_map type_map = {};
        zaclr_object_handle parent_handle = 0u;
        zaclr_object_handle child_a_handle = 0u;
        zaclr_object_handle child_b_handle = 0u;
        struct zaclr_reference_object_desc* parent;
        struct zaclr_stack_value field_a = {};
        struct zaclr_stack_value field_b = {};
        struct zaclr_result result = zaclr_heap_initialize(&runtime.heap, &runtime);

        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        type_desc.id = 1u;
        type_desc.token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | 1u);
        type_desc.field_count = 2u;
        type_map.types = &type_desc;
        type_map.count = 1u;
        assembly.type_map = type_map;

        result = zaclr_reference_object_allocate(&runtime.heap,
                                                 &assembly,
                                                 type_desc.id,
                                                 type_desc.token,
                                                 type_desc.field_count,
                                                 &parent_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 2u, zaclr_token_make(0u), 0u, &child_a_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 3u, zaclr_token_make(0u), 0u, &child_b_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        parent = zaclr_reference_object_from_handle(&runtime.heap, parent_handle);
        if (parent == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = expect(parent->object.owning_assembly == &assembly && parent->field_capacity == 2u);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        field_a.kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        field_a.data.object_handle = child_a_handle;
        result = zaclr_reference_object_store_field(parent,
                                                    zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | 1u),
                                                    &field_a);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        field_b.kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        field_b.data.object_handle = child_b_handle;
        result = zaclr_reference_object_store_field(parent,
                                                    zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | 2u),
                                                    &field_b);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        runtime.boot_launch.thread.current_exception = parent_handle;
        result = zaclr_gc_collect(&runtime);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = expect(zaclr_heap_get_object(&runtime.heap, child_a_handle) != NULL
                        && zaclr_heap_get_object(&runtime.heap, child_b_handle) != NULL);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        field_b.data.object_handle = parent_handle;
        result = zaclr_reference_object_store_field(parent,
                                                    zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | 3u),
                                                    &field_b);
        return expect(result.status == ZACLR_STATUS_BUFFER_TOO_SMALL);
    }

    static struct zaclr_result run_recursive_cycle_collection_test(void)
    {
        struct zaclr_runtime runtime = {};
        zaclr_object_handle first_handle = 0u;
        zaclr_object_handle second_handle = 0u;
        struct zaclr_reference_object_desc* first;
        struct zaclr_reference_object_desc* second;
        struct zaclr_stack_value first_to_second = {};
        struct zaclr_stack_value second_to_first = {};
        struct zaclr_result result = zaclr_heap_initialize(&runtime.heap, &runtime);

        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 1u, zaclr_token_make(0u), 1u, &first_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 2u, zaclr_token_make(0u), 1u, &second_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        first = zaclr_reference_object_from_handle(&runtime.heap, first_handle);
        second = zaclr_reference_object_from_handle(&runtime.heap, second_handle);
        if (first == NULL || second == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_EXEC);
        }

        first_to_second.kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        first_to_second.data.object_handle = second_handle;
        result = zaclr_reference_object_store_field(first,
                                                    zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | 1u),
                                                    &first_to_second);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        second_to_first.kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        second_to_first.data.object_handle = first_handle;
        result = zaclr_reference_object_store_field(second,
                                                    zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | 1u),
                                                    &second_to_first);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_gc_collect(&runtime);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return expect(zaclr_heap_get_object(&runtime.heap, first_handle) == NULL
                      && zaclr_heap_get_object(&runtime.heap, second_handle) == NULL
                      && zaclr_heap_live_object_count(&runtime.heap) == 0u);
    }

    static struct zaclr_result run_mixed_root_category_survival_test(void)
    {
        struct zaclr_runtime runtime = {};
        struct zaclr_frame frame = {};
        struct zaclr_stack_value arguments[1] = {};
        struct zaclr_stack_value locals[1] = {};
        struct zaclr_stack_value eval_values[1] = {};
        struct zaclr_stack_value static_fields[1] = {};
        struct zaclr_loaded_assembly assembly = {};
        zaclr_object_handle arg_handle = 0u;
        zaclr_object_handle local_handle = 0u;
        zaclr_object_handle eval_handle = 0u;
        zaclr_object_handle exception_handle = 0u;
        zaclr_object_handle static_handle = 0u;
        struct zaclr_result result = zaclr_heap_initialize(&runtime.heap, &runtime);

        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 1u, zaclr_token_make(0u), 0u, &arg_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 2u, zaclr_token_make(0u), 0u, &local_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 3u, zaclr_token_make(0u), 0u, &eval_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 4u, zaclr_token_make(0u), 0u, &exception_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 5u, zaclr_token_make(0u), 0u, &static_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        arguments[0].kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        arguments[0].data.object_handle = arg_handle;
        locals[0].kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        locals[0].data.object_handle = local_handle;
        eval_values[0].kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        eval_values[0].data.object_handle = eval_handle;
        static_fields[0].kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        static_fields[0].data.object_handle = static_handle;

        frame.arguments = arguments;
        frame.argument_count = 1u;
        frame.locals = locals;
        frame.local_count = 1u;
        frame.eval_stack.values = eval_values;
        frame.eval_stack.depth = 1u;
        runtime.active_frame = &frame;
        runtime.boot_launch.thread.current_exception = exception_handle;
        assembly.static_fields = static_fields;
        assembly.static_field_count = 1u;
        runtime.assemblies.entries = &assembly;
        runtime.assemblies.count = 1u;
        runtime.assemblies.capacity = 1u;

        result = zaclr_gc_collect(&runtime);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return expect(zaclr_heap_get_object(&runtime.heap, arg_handle) != NULL
                      && zaclr_heap_get_object(&runtime.heap, local_handle) != NULL
                      && zaclr_heap_get_object(&runtime.heap, eval_handle) != NULL
                      && zaclr_heap_get_object(&runtime.heap, exception_handle) != NULL
                      && zaclr_heap_get_object(&runtime.heap, static_handle) != NULL);
    }

    static struct zaclr_result run_handle_table_root_survival_test(void)
    {
        struct zaclr_runtime runtime = {};
        zaclr_object_handle handle = 0u;
        uint32_t handle_index = 0u;
        struct zaclr_result result = zaclr_heap_initialize(&runtime.heap, &runtime);

        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_handle_table_initialize(&runtime.boot_launch.handle_table, 1u, 2u);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_reference_object_allocate(&runtime.heap, NULL, 1u, zaclr_token_make(0u), 0u, &handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            zaclr_handle_table_reset(&runtime.boot_launch.handle_table);
            return result;
        }

        result = zaclr_handle_table_store(&runtime.boot_launch.handle_table, handle, &handle_index);
        if (result.status != ZACLR_STATUS_OK)
        {
            zaclr_handle_table_reset(&runtime.boot_launch.handle_table);
            return result;
        }

        result = expect(handle_index == 0u && zaclr_handle_table_count(&runtime.boot_launch.handle_table) == 1u);
        if (result.status != ZACLR_STATUS_OK)
        {
            zaclr_handle_table_reset(&runtime.boot_launch.handle_table);
            return result;
        }

        result = zaclr_gc_collect(&runtime);
        if (result.status != ZACLR_STATUS_OK)
        {
            zaclr_handle_table_reset(&runtime.boot_launch.handle_table);
            return result;
        }

        result = expect(zaclr_heap_get_object(&runtime.heap, handle) != NULL);
        zaclr_handle_table_reset(&runtime.boot_launch.handle_table);
        return result;
    }

    static struct zaclr_result run_frame_style_root_enumeration_test(void)
    {
        struct zaclr_runtime runtime = {};
        struct zaclr_frame frame = {};
        struct zaclr_stack_value arguments[1] = {};
        struct zaclr_stack_value locals[1] = {};
        struct zaclr_stack_value eval_values[1] = {};
        struct zaclr_gc_root_visitor visitor = {};
        struct root_counter_context counter = {};

        arguments[0].kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        arguments[0].data.object_handle = 11u;
        locals[0].kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        locals[0].data.object_handle = 12u;
        eval_values[0].kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        eval_values[0].data.object_handle = 13u;

        frame.arguments = arguments;
        frame.argument_count = 1u;
        frame.locals = locals;
        frame.local_count = 1u;
        frame.eval_stack.values = eval_values;
        frame.eval_stack.depth = 1u;
        runtime.boot_launch.thread.current_exception = 14u;

        visitor.visit_handle = count_root;
        visitor.context = &counter;
        zaclr_gc_enumerate_thread_roots(&runtime.boot_launch.thread, &frame, &visitor);
        return expect(counter.count == 4u);
    }
}

extern "C" struct zaclr_result zaclr_run_dispatch_tests(void)
{
    struct zaclr_result result = run_heap_retry_smoke_test();
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = run_reference_object_graph_test();
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = run_unrooted_object_collection_test();
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = run_static_root_survival_test();
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = run_boxed_value_survival_test();
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = run_dynamic_field_capacity_test();
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = run_recursive_cycle_collection_test();
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = run_mixed_root_category_survival_test();
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = run_handle_table_root_survival_test();
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return run_frame_style_root_enumeration_test();
}
