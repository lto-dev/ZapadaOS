#include <kernel/zaclr/heap/zaclr_gc.h>

#include <kernel/zaclr/heap/zaclr_array.h>
#include <kernel/zaclr/heap/zaclr_gc_roots.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/exec/zaclr_engine.h>
#include <kernel/zaclr/metadata/zaclr_method_map.h>
#include <kernel/zaclr/metadata/zaclr_token.h>
#include <kernel/zaclr/metadata/zaclr_type_map.h>
#include <kernel/zaclr/process/zaclr_handle_table.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

namespace
{
    static constexpr uint8_t k_finalizer_state_mask = ZACLR_OBJECT_GC_STATE_FINALIZER_PENDING
                                                    | ZACLR_OBJECT_GC_STATE_FINALIZER_QUEUED
                                                    | ZACLR_OBJECT_GC_STATE_FINALIZER_COMPLETE;
    static constexpr uint8_t k_has_this_calling_convention = 0x20u;

    struct zaclr_gc_mark_context {
        struct zaclr_runtime* runtime;
    };

    static void zaclr_gc_mark_handle(struct zaclr_runtime* runtime, zaclr_object_handle handle);

    static void zaclr_gc_mark_array_children(struct zaclr_runtime* runtime,
                                             struct zaclr_array_desc* array)
    {
        uint32_t index;
        zaclr_object_handle* elements;

        if (runtime == NULL || array == NULL || !zaclr_object_contains_references(&array->object))
        {
            return;
        }

        if (array->element_size != sizeof(zaclr_object_handle))
        {
            return;
        }

        elements = (zaclr_object_handle*)zaclr_array_data(array);
        if (elements == NULL)
        {
            return;
        }

        for (index = 0u; index < array->length; ++index)
        {
            zaclr_gc_mark_handle(runtime, elements[index]);
        }
    }

    static void zaclr_gc_mark_reference_object_children(struct zaclr_runtime* runtime,
                                                        struct zaclr_reference_object_desc* object)
    {
        uint32_t index;
        const uint32_t* field_tokens;
        const struct zaclr_stack_value* field_values;

        if (runtime == NULL || object == NULL)
        {
            return;
        }

        field_tokens = zaclr_reference_object_field_tokens_const(object);
        field_values = zaclr_reference_object_field_values_const(object);

        for (index = 0u; index < object->field_capacity; ++index)
        {
            const struct zaclr_stack_value* value = &field_values[index];
            if (field_tokens[index] == 0u)
            {
                continue;
            }

            if (zaclr_stack_value_contains_references(value) != 0u)
            {
                zaclr_gc_mark_handle(runtime, value->data.object_handle);
            }
        }
    }

    static void zaclr_gc_mark_boxed_value_children(struct zaclr_runtime* runtime,
                                                   struct zaclr_boxed_value_desc* boxed_value)
    {
        if (runtime == NULL || boxed_value == NULL)
        {
            return;
        }

        if (zaclr_stack_value_contains_references(&boxed_value->value) != 0u)
        {
            zaclr_gc_mark_handle(runtime, boxed_value->value.data.object_handle);
        }
    }

    static void zaclr_gc_mark_object_children(struct zaclr_runtime* runtime,
                                              struct zaclr_object_desc* object)
    {
        if (runtime == NULL || object == NULL)
        {
            return;
        }

        switch ((enum zaclr_object_family)zaclr_object_family(object))
        {
            case ZACLR_OBJECT_FAMILY_ARRAY:
                zaclr_gc_mark_array_children(runtime, (struct zaclr_array_desc*)object);
                break;

            case ZACLR_OBJECT_FAMILY_STRING:
                break;

            case ZACLR_OBJECT_FAMILY_INSTANCE:
                if ((zaclr_object_flags(object) & ZACLR_OBJECT_FLAG_BOXED_VALUE) != 0u)
                {
                    zaclr_gc_mark_boxed_value_children(runtime, (struct zaclr_boxed_value_desc*)object);
                }
                else if ((zaclr_object_flags(object) & ZACLR_OBJECT_FLAG_REFERENCE_TYPE) != 0u)
                {
                    zaclr_gc_mark_reference_object_children(runtime, (struct zaclr_reference_object_desc*)object);
                }
                break;

            case ZACLR_OBJECT_FAMILY_UNKNOWN:
            default:
                break;
        }
    }

    static void zaclr_gc_mark_handle(struct zaclr_runtime* runtime, zaclr_object_handle handle)
    {
        struct zaclr_object_desc* object;

        if (runtime == NULL || handle == 0u)
        {
            return;
        }

        object = zaclr_heap_get_object(&runtime->heap, handle);
        if (object == NULL || zaclr_object_is_marked(object) != 0u)
        {
            return;
        }

        zaclr_object_set_marked(object, 1u);
        zaclr_gc_mark_object_children(runtime, object);
    }

    static void zaclr_gc_visit_root_handle(zaclr_object_handle* slot,
                                           uint32_t,
                                           void* context)
    {
        struct zaclr_gc_mark_context* mark_context = (struct zaclr_gc_mark_context*)context;
        if (slot == NULL || mark_context == NULL)
        {
            return;
        }

        zaclr_gc_mark_handle(mark_context->runtime, *slot);
    }

    static void zaclr_gc_clear_short_weak_handles(struct zaclr_runtime* runtime)
    {
        uint32_t index;
        struct zaclr_handle_table* table;

        if (runtime == NULL)
        {
            return;
        }

        table = &runtime->boot_launch.handle_table;
        if (table == NULL || table->entries == NULL)
        {
            return;
        }

        for (index = 0u; index < table->count; ++index)
        {
            struct zaclr_gc_handle_entry* entry = &table->entries[index];
            struct zaclr_object_desc* object;
            if (entry->handle == 0u)
            {
                continue;
            }

            if (entry->kind != ZACLR_GC_HANDLE_KIND_WEAK)
            {
                continue;
            }

            object = zaclr_heap_get_object(&runtime->heap, entry->handle);
            if (object == NULL || zaclr_object_is_marked(object) == 0u)
            {
                entry->handle = 0u;
            }
        }
    }

    static void zaclr_gc_clear_long_weak_handles(struct zaclr_runtime* runtime)
    {
        uint32_t index;
        struct zaclr_handle_table* table;

        if (runtime == NULL)
        {
            return;
        }

        table = &runtime->boot_launch.handle_table;
        if (table == NULL || table->entries == NULL)
        {
            return;
        }

        for (index = 0u; index < table->count; ++index)
        {
            struct zaclr_gc_handle_entry* entry = &table->entries[index];
            if (entry->handle == 0u || entry->kind != ZACLR_GC_HANDLE_KIND_WEAK_TRACK_RESURRECTION)
            {
                continue;
            }

            if (zaclr_heap_get_object(&runtime->heap, entry->handle) == NULL)
            {
                entry->handle = 0u;
            }
        }
    }

    static const struct zaclr_type_desc* zaclr_gc_get_object_type(const struct zaclr_object_desc* object)
    {
        struct zaclr_token token;

        if (object == NULL || object->owning_assembly == NULL || object->type_id == 0u)
        {
            return NULL;
        }

        token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | (uint32_t)object->type_id);
        return zaclr_type_map_find_by_token(&object->owning_assembly->type_map, token);
    }

    static uint32_t zaclr_gc_object_has_finalizer(const struct zaclr_object_desc* object)
    {
        const struct zaclr_type_desc* type = zaclr_gc_get_object_type(object);
        return type != NULL && (zaclr_type_runtime_flags(type) & ZACLR_TYPE_RUNTIME_FLAG_HAS_FINALIZER) != 0u;
    }

    static const struct zaclr_method_desc* zaclr_gc_get_finalizer_method(const struct zaclr_object_desc* object)
    {
        const struct zaclr_type_desc* type;
        uint32_t index;

        if (object == NULL || object->owning_assembly == NULL)
        {
            return NULL;
        }

        type = zaclr_gc_get_object_type(object);
        if (type == NULL || object->owning_assembly->method_map.methods == NULL)
        {
            return NULL;
        }

        for (index = 0u; index < type->method_count; ++index)
        {
            const struct zaclr_method_desc* method = &object->owning_assembly->method_map.methods[type->first_method_index + index];
            if (method->name.text != NULL
                && zaclr_text_equals(method->name.text, "Finalize")
                && method->signature.parameter_count == 0u
                && (method->signature.calling_convention & k_has_this_calling_convention) != 0u)
            {
                return method;
            }
        }

        return NULL;
    }

    static void zaclr_gc_remove_handle_from_table(struct zaclr_handle_table* table, zaclr_object_handle handle)
    {
        uint32_t index;

        if (table == NULL || handle == 0u || table->entries == NULL)
        {
            return;
        }

        for (index = 0u; index < table->count; ++index)
        {
            if (table->entries[index].handle == handle)
            {
                table->entries[index].handle = 0u;
                table->entries[index].kind = 0u;
                table->entries[index].flags = 0u;
                table->entries[index].reserved = 0u;
            }
        }
    }

    static struct zaclr_result zaclr_gc_promote_unreachable_finalizable_objects(struct zaclr_runtime* runtime)
    {
        uint32_t index;

        if (runtime == NULL || runtime->heap.slots == NULL)
        {
            return zaclr_result_ok();
        }

        for (index = 0u; index < runtime->heap.slot_count; ++index)
        {
            struct zaclr_heap_slot* slot = &runtime->heap.slots[index];
            struct zaclr_object_desc* object;
            uint32_t queue_index;
            struct zaclr_result result;

            if (slot->used == 0u || slot->object == NULL)
            {
                continue;
            }

            object = slot->object;
            if (zaclr_object_is_marked(object) != 0u
                || (object->gc_state & ZACLR_OBJECT_GC_STATE_FINALIZER_PENDING) == 0u
                || (object->gc_state & ZACLR_OBJECT_GC_STATE_FINALIZER_QUEUED) != 0u)
            {
                continue;
            }

            result = zaclr_handle_table_store_ex(&runtime->finalizer_queue,
                                                 object->handle,
                                                 ZACLR_GC_HANDLE_KIND_STRONG,
                                                 &queue_index);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            (void)queue_index;
            object->gc_state = (uint8_t)(object->gc_state | ZACLR_OBJECT_GC_STATE_FINALIZER_QUEUED);
            zaclr_gc_mark_handle(runtime, object->handle);
        }

        return zaclr_result_ok();
    }
}

extern "C" struct zaclr_result zaclr_gc_collect(struct zaclr_runtime* runtime)
{
    struct zaclr_gc_mark_context mark_context;
    struct zaclr_gc_root_visitor visitor;

    if (runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    if ((runtime->heap.flags & ZACLR_HEAP_FLAG_GC_ACTIVE) != 0u)
    {
        return zaclr_result_ok();
    }

    runtime->heap.flags |= ZACLR_HEAP_FLAG_GC_ACTIVE;
    zaclr_heap_clear_marks(&runtime->heap);

    mark_context.runtime = runtime;
    visitor.visit_handle = zaclr_gc_visit_root_handle;
    visitor.context = &mark_context;

    zaclr_gc_enumerate_runtime_roots(runtime, &visitor);
    zaclr_gc_clear_short_weak_handles(runtime);

    {
        struct zaclr_result result = zaclr_gc_promote_unreachable_finalizable_objects(runtime);
        if (result.status != ZACLR_STATUS_OK)
        {
            runtime->heap.flags &= ~ZACLR_HEAP_FLAG_GC_ACTIVE;
            return result;
        }
    }

    zaclr_heap_sweep_unmarked(&runtime->heap);
    zaclr_gc_clear_long_weak_handles(runtime);

    runtime->heap.flags &= ~ZACLR_HEAP_FLAG_GC_ACTIVE;
    return zaclr_result_ok();
}

extern "C" uint32_t zaclr_gc_collection_count(const struct zaclr_runtime* runtime)
{
    return runtime != NULL ? runtime->heap.collection_count : 0u;
}

extern "C" struct zaclr_result zaclr_gc_wait_for_pending_finalizers(struct zaclr_runtime* runtime)
{
    uint32_t index;

    if (runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    for (index = 0u; index < runtime->finalizer_queue.count; ++index)
    {
        struct zaclr_gc_handle_entry* entry = &runtime->finalizer_queue.entries[index];
        struct zaclr_object_desc* object;

        if (entry->handle == 0u)
        {
            continue;
        }

        object = zaclr_heap_get_object(&runtime->heap, entry->handle);
        if (object != NULL)
        {
            const struct zaclr_method_desc* finalizer = zaclr_gc_get_finalizer_method(object);
            if (finalizer != NULL)
            {
                struct zaclr_result invoke_status = zaclr_engine_execute_instance_method(&runtime->engine,
                                                                                         runtime,
                                                                                         &runtime->boot_launch,
                                                                                         object->owning_assembly,
                                                                                         finalizer,
                                                                                         object->handle);
                if (invoke_status.status != ZACLR_STATUS_OK)
                {
                    return invoke_status;
                }
            }

            object->gc_state = (uint8_t)((object->gc_state & ~(ZACLR_OBJECT_GC_STATE_FINALIZER_PENDING | ZACLR_OBJECT_GC_STATE_FINALIZER_QUEUED))
                                       | ZACLR_OBJECT_GC_STATE_FINALIZER_COMPLETE);
        }

        entry->handle = 0u;
        entry->kind = 0u;
        entry->flags = 0u;
        entry->reserved = 0u;
    }

    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_gc_suppress_finalize(struct zaclr_runtime* runtime, zaclr_object_handle handle)
{
    struct zaclr_object_desc* object;

    if (runtime == NULL || handle == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    object = zaclr_heap_get_object(&runtime->heap, handle);
    if (object == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    object->gc_state = (uint8_t)(object->gc_state & ~k_finalizer_state_mask);
    zaclr_gc_remove_handle_from_table(&runtime->finalizer_queue, handle);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_gc_reregister_for_finalize(struct zaclr_runtime* runtime, zaclr_object_handle handle)
{
    struct zaclr_object_desc* object;

    if (runtime == NULL || handle == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    object = zaclr_heap_get_object(&runtime->heap, handle);
    if (object == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    if (zaclr_gc_object_has_finalizer(object) == 0u)
    {
        return zaclr_result_ok();
    }

    object->gc_state = (uint8_t)((object->gc_state & ~k_finalizer_state_mask)
                               | ZACLR_OBJECT_GC_STATE_FINALIZER_PENDING);
    zaclr_gc_remove_handle_from_table(&runtime->finalizer_queue, handle);
    return zaclr_result_ok();
}
