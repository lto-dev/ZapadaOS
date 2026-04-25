#include <kernel/zaclr/heap/zaclr_gc_roots.h>

#include <kernel/zaclr/process/zaclr_handle_table.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

namespace
{
    static void zaclr_gc_visit_stack_values(struct zaclr_stack_value* values,
                                            uint32_t count,
                                            uint32_t flags,
                                            struct zaclr_gc_root_visitor* visitor)
    {
        uint32_t index;

        if (values == NULL || visitor == NULL || visitor->visit_object_reference == NULL)
        {
            return;
        }

        for (index = 0u; index < count; ++index)
        {
            struct zaclr_stack_value* value = &values[index];
            if (value->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE && value->data.object_reference != NULL)
            {
                visitor->visit_object_reference(&value->data.object_reference, flags, visitor->context);
            }
        }
    }

    static void zaclr_gc_enumerate_assembly_static_roots(const struct zaclr_loaded_assembly* assembly,
                                                         struct zaclr_gc_root_visitor* visitor)
    {
        if (assembly == NULL || visitor == NULL)
        {
            return;
        }

        zaclr_gc_visit_stack_values(assembly->static_fields,
                                    assembly->static_field_count,
                                    ZACLR_GC_ROOT_FLAG_STATIC,
                                    visitor);

        if (visitor->visit_object_reference != NULL)
        {
            if (assembly->runtime_type_cache != NULL)
            {
                for (uint32_t index = 0u; index < assembly->runtime_type_cache_count; ++index)
                {
                    struct zaclr_object_desc* object = zaclr_heap_get_object(NULL, assembly->runtime_type_cache[index]);
                    if (object != NULL)
                    {
                        visitor->visit_object_reference(&object,
                                                        ZACLR_GC_ROOT_FLAG_STATIC,
                                                        visitor->context);
                    }
                }
            }

            if (assembly->exposed_assembly_handle != 0u)
            {
                struct zaclr_object_desc* object = zaclr_heap_get_object(NULL, assembly->exposed_assembly_handle);
                if (object != NULL)
                {
                    visitor->visit_object_reference(&object,
                                                    ZACLR_GC_ROOT_FLAG_STATIC,
                                                    visitor->context);
                }
            }
        }
    }

    static void zaclr_gc_enumerate_handle_table_roots(struct zaclr_handle_table* handle_table,
                                                      struct zaclr_gc_root_visitor* visitor)
    {
        uint32_t index;
        struct zaclr_gc_handle_entry* entry;

        if (handle_table == NULL || visitor == NULL || visitor->visit_object_reference == NULL)
        {
            return;
        }

        for (index = 0u; index < handle_table->count; ++index)
        {
            entry = &handle_table->entries[index];
            if (entry->handle == 0u)
            {
                continue;
            }

            if (entry->kind == ZACLR_GC_HANDLE_KIND_WEAK
                || entry->kind == ZACLR_GC_HANDLE_KIND_WEAK_TRACK_RESURRECTION)
            {
                continue;
            }

            {
                struct zaclr_object_desc* object = zaclr_heap_get_object(NULL, entry->handle);
                if (object != NULL)
                {
                    visitor->visit_object_reference(&object,
                                                    ZACLR_GC_ROOT_FLAG_HANDLE_TABLE,
                                                    visitor->context);
                }
            }
        }
    }
}

extern "C" void zaclr_gc_enumerate_frame_roots(struct zaclr_frame* frame,
                                                 struct zaclr_gc_root_visitor* visitor)
{
    if (frame == NULL || visitor == NULL)
    {
        return;
    }

    zaclr_gc_visit_stack_values(frame->arguments,
                                frame->argument_count,
                                ZACLR_GC_ROOT_FLAG_ARGUMENT,
                                visitor);
    zaclr_gc_visit_stack_values(frame->locals,
                                frame->local_count,
                                ZACLR_GC_ROOT_FLAG_LOCAL,
                                visitor);
    zaclr_gc_visit_stack_values(frame->eval_stack.values,
                                frame->eval_stack.depth,
                                ZACLR_GC_ROOT_FLAG_EVAL_STACK,
                                visitor);
}

extern "C" void zaclr_gc_enumerate_thread_roots(struct zaclr_thread* thread,
                                                  struct zaclr_frame* current_frame,
                                                  struct zaclr_gc_root_visitor* visitor)
{
    struct zaclr_frame* frame;

    if (thread == NULL || visitor == NULL || visitor->visit_object_reference == NULL)
    {
        return;
    }

    if (thread->current_exception != NULL)
    {
        visitor->visit_object_reference(&thread->current_exception,
                                        ZACLR_GC_ROOT_FLAG_THREAD_EXCEPTION,
                                        visitor->context);
    }

    for (frame = current_frame; frame != NULL; frame = frame->parent)
    {
        zaclr_gc_enumerate_frame_roots(frame, visitor);
    }
}

extern "C" void zaclr_gc_enumerate_runtime_roots(struct zaclr_runtime* runtime,
                                                    struct zaclr_gc_root_visitor* visitor)
{
    uint32_t assembly_index;

    if (runtime == NULL || visitor == NULL)
    {
        return;
    }

    zaclr_gc_enumerate_handle_table_roots(&runtime->finalizer_queue, visitor);
    zaclr_gc_enumerate_handle_table_roots(&runtime->boot_launch.handle_table, visitor);
    zaclr_gc_enumerate_thread_roots(&runtime->boot_launch.thread, runtime->active_frame, visitor);

    struct zaclr_app_domain* domain = zaclr_runtime_current_domain(runtime);
    if (domain != NULL)
    {
        for (assembly_index = 0u; assembly_index < domain->registry.count; ++assembly_index)
        {
            zaclr_gc_enumerate_assembly_static_roots(&domain->registry.entries[assembly_index], visitor);
        }
    }
}
