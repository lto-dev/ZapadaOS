#include "zaclr_native_System_Runtime_InteropServices_GCHandle.h"

#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/process/zaclr_handle_table.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static const uint64_t k_handle_shift = 1u;

    static struct zaclr_handle_table* gc_handle_table(struct zaclr_native_call_frame& frame)
    {
        return frame.runtime != NULL ? &frame.runtime->boot_launch.handle_table : NULL;
    }

    static bool decode_handle_value(int64_t value, uint32_t* out_index)
    {
        uint64_t raw;

        if (out_index == NULL || value <= 0)
        {
            return false;
        }

        raw = (uint64_t)value;
        if ((raw & 0x1u) != 0u)
        {
            return false;
        }

        raw >>= k_handle_shift;
        if (raw == 0u || raw > 0xFFFFFFFFu)
        {
            return false;
        }

        *out_index = (uint32_t)(raw - 1u);
        return true;
    }

    static bool try_decode_handle_table_pointer(struct zaclr_handle_table* table,
                                                int64_t handle_value,
                                                uint32_t* out_index)
    {
        uintptr_t raw_handle;
        uintptr_t base;
        uintptr_t end;

        if (table == NULL || out_index == NULL || table->entries == NULL || table->capacity == 0u || handle_value <= 0)
        {
            return false;
        }

        raw_handle = (uintptr_t)handle_value;
        base = (uintptr_t)&table->entries[0].handle;
        end = (uintptr_t)&table->entries[table->capacity].handle;
        if (raw_handle < base || raw_handle >= end)
        {
            return false;
        }

        if (((raw_handle - base) % sizeof(struct zaclr_gc_handle_entry)) != 0u)
        {
            return false;
        }

        *out_index = (uint32_t)((raw_handle - base) / sizeof(struct zaclr_gc_handle_entry));
        return *out_index < table->count;
    }

    static struct zaclr_result get_entry(struct zaclr_native_call_frame& frame,
                                         int64_t handle_value,
                                         uint32_t* out_index,
                                         struct zaclr_gc_handle_entry* out_entry)
    {
        struct zaclr_handle_table* table = gc_handle_table(frame);
        uint32_t index;
        struct zaclr_result status;

        if (table == NULL || out_index == NULL || out_entry == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
        }

        if (try_decode_handle_table_pointer(table, handle_value, &index))
        {
            status = zaclr_handle_table_load_entry(table, index, out_entry);
            if (status.status != ZACLR_STATUS_OK)
            {
                return status;
            }

            if (out_entry->handle == 0u)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_PROCESS);
            }

            *out_index = index;
            return zaclr_result_ok();
        }

        if (!decode_handle_value(handle_value, &index))
        {

            zaclr_object_handle object_handle = handle_value > 0 ? (zaclr_object_handle)handle_value : 0u;
            for (index = 0u; index < table->count; ++index)
            {
                if (table->entries[index].handle == object_handle)
                {
                    *out_entry = table->entries[index];
                    *out_index = index;
                    return zaclr_result_ok();
                }
            }

            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
        }

        status = zaclr_handle_table_load_entry(table, index, out_entry);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        if (out_entry->handle == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_PROCESS);
        }

        *out_index = index;
        return zaclr_result_ok();
    }

    static void update_pinned_state(struct zaclr_native_call_frame& frame,
                                    const struct zaclr_gc_handle_entry* old_entry,
                                    const struct zaclr_gc_handle_entry* new_entry)
    {
        struct zaclr_object_desc* object;
        struct zaclr_handle_table* table = gc_handle_table(frame);
        (void)table;

        if (frame.runtime == NULL)
        {
            return;
        }

        if (old_entry != NULL && old_entry->kind == ZACLR_GC_HANDLE_KIND_PINNED && old_entry->handle != 0u)
        {
            object = zaclr_heap_get_object(&frame.runtime->heap, old_entry->handle);
            if (object != NULL)
            {
                object->gc_state = (uint8_t)(object->gc_state & ~ZACLR_OBJECT_GC_STATE_PINNED);
            }
        }

        if (new_entry != NULL && new_entry->kind == ZACLR_GC_HANDLE_KIND_PINNED && new_entry->handle != 0u)
        {
            object = zaclr_heap_get_object(&frame.runtime->heap, new_entry->handle);
            if (object != NULL)
            {
                object->gc_state = (uint8_t)(object->gc_state | ZACLR_OBJECT_GC_STATE_PINNED);
            }
        }
    }
}

struct zaclr_result zaclr_native_System_Runtime_InteropServices_GCHandle::_InternalAlloc___STATIC__I__OBJECT__I4(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle handle;
    int32_t type;
    uint32_t index;
    uint32_t kind;
    struct zaclr_handle_table* table = gc_handle_table(frame);
    struct zaclr_result status;
    struct zaclr_gc_handle_entry entry = {};

    if (table == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleAlloc.TablePtr",
                      (uint64_t)(uintptr_t)table);

    status = zaclr_native_call_frame_arg_object(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleAlloc.ObjectHandle",
                      (uint64_t)handle);

    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &type);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleAlloc.TypeArg",
                      (uint64_t)(uint32_t)type);

    switch (type)
    {
        case 0: kind = ZACLR_GC_HANDLE_KIND_WEAK; break;
        case 1: kind = ZACLR_GC_HANDLE_KIND_WEAK_TRACK_RESURRECTION; break;
        case 2: kind = ZACLR_GC_HANDLE_KIND_STRONG; break;
        case 3: kind = ZACLR_GC_HANDLE_KIND_PINNED; break;
        default:
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    status = zaclr_handle_table_store_ex(table, handle, kind, &index);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleAlloc.StoredIndex",
                      (uint64_t)index);

    entry.handle = handle;
    entry.kind = (uint8_t)kind;
    update_pinned_state(frame, NULL, &entry);
    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleAlloc.ReturnEncodedHandle",
                      (uint64_t)(uintptr_t)&table->entries[index].handle);
    return zaclr_native_call_frame_set_i8(&frame, (int64_t)(uintptr_t)&table->entries[index].handle);
}

struct zaclr_result zaclr_native_System_Runtime_InteropServices_GCHandle::_InternalFree___STATIC__BOOLEAN__I(struct zaclr_native_call_frame& frame)
{
    int64_t handle_value;
    uint32_t index;
    struct zaclr_gc_handle_entry entry;
    struct zaclr_handle_table* table = gc_handle_table(frame);
    struct zaclr_result status;

    if (table == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    status = zaclr_native_call_frame_arg_i8(&frame, 0u, &handle_value);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = get_entry(frame, handle_value, &index, &entry);
    if (status.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_bool(&frame, false);
    }

    update_pinned_state(frame, &entry, NULL);
    table->entries[index] = {};
    return zaclr_native_call_frame_set_bool(&frame, true);
}

struct zaclr_result zaclr_native_System_Runtime_InteropServices_GCHandle::GCHandle_InternalFreeWithGCTransition___STATIC__VOID__I(struct zaclr_native_call_frame& frame)
{
    int64_t handle_value;
    uint32_t index;
    struct zaclr_gc_handle_entry entry;
    struct zaclr_handle_table* table = gc_handle_table(frame);
    struct zaclr_result status;

    if (table == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    status = zaclr_native_call_frame_arg_i8(&frame, 0u, &handle_value);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = get_entry(frame, handle_value, &index, &entry);
    if (status.status == ZACLR_STATUS_OK)
    {
        update_pinned_state(frame, &entry, NULL);
        table->entries[index] = {};
    }

    return zaclr_native_call_frame_set_void(&frame);
}

struct zaclr_result zaclr_native_System_Runtime_InteropServices_GCHandle::InternalGet___STATIC__OBJECT__I(struct zaclr_native_call_frame& frame)
{
    int64_t handle_value;
    uint32_t index;
    struct zaclr_gc_handle_entry entry;
    struct zaclr_result status = zaclr_native_call_frame_arg_i8(&frame, 0u, &handle_value);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = get_entry(frame, handle_value, &index, &entry);
    if (status.status != ZACLR_STATUS_OK)
    {
        ZACLR_TRACE_VALUE(frame.runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "GCHandleGet.ResolveStatus",
                          (uint64_t)status.status);
        return zaclr_native_call_frame_set_object(&frame, 0u);
    }

    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleGet.HandleValue",
                      (uint64_t)handle_value);
    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleGet.Index",
                      (uint64_t)index);
    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleGet.Kind",
                      (uint64_t)entry.kind);
    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleGet.ObjectHandle",
                      (uint64_t)entry.handle);

    if (entry.handle != 0u && zaclr_heap_get_object(&frame.runtime->heap, entry.handle) == NULL)
    {
        ZACLR_TRACE_VALUE(frame.runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "GCHandleGet.LiveObjectMissing",
                          (uint64_t)entry.handle);
        return zaclr_native_call_frame_set_object(&frame, 0u);
    }

    return zaclr_native_call_frame_set_object(&frame, entry.handle);
}

struct zaclr_result zaclr_native_System_Runtime_InteropServices_GCHandle::InternalSet___STATIC__VOID__I__OBJECT(struct zaclr_native_call_frame& frame)
{
    int64_t handle_value;
    zaclr_object_handle handle_argument_object;
    zaclr_object_handle handle;
    uint32_t index;
    struct zaclr_gc_handle_entry old_entry;
    struct zaclr_gc_handle_entry new_entry;
    struct zaclr_handle_table* table = gc_handle_table(frame);
    struct zaclr_result status;

    if (table == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    if (frame.runtime != NULL)
    {
        struct zaclr_stack_value* arg0 = zaclr_native_call_frame_arg(&frame, 0u);
        struct zaclr_stack_value* arg1 = zaclr_native_call_frame_arg(&frame, 1u);
        ZACLR_TRACE_VALUE(frame.runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "GCHandleSet.Arg0Kind",
                          (uint64_t)(arg0 != NULL ? arg0->kind : 0xFFFFFFFFu));
        ZACLR_TRACE_VALUE(frame.runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "GCHandleSet.Arg1Kind",
                          (uint64_t)(arg1 != NULL ? arg1->kind : 0xFFFFFFFFu));
    }

    status = zaclr_native_call_frame_arg_i8(&frame, 0u, &handle_value);
    if (status.status != ZACLR_STATUS_OK)
    {
        status = zaclr_native_call_frame_arg_object(&frame, 0u, &handle_argument_object);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        handle_value = (int64_t)handle_argument_object;

        if (frame.runtime != NULL)
        {
            struct zaclr_boxed_value_desc* boxed_value = zaclr_boxed_value_from_handle(&frame.runtime->heap,
                                                                                       handle_argument_object);
            if (boxed_value != NULL)
            {
                if (boxed_value->value.kind == ZACLR_STACK_VALUE_I8)
                {
                    handle_value = boxed_value->value.data.i8;
                }
                else if (boxed_value->value.kind == ZACLR_STACK_VALUE_I4)
                {
                    handle_value = boxed_value->value.data.i4;
                }
            }
        }
    }

    status = zaclr_native_call_frame_arg_object(&frame, 1u, &handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = get_entry(frame, handle_value, &index, &old_entry);
    if (status.status != ZACLR_STATUS_OK)
    {
        if (table->count != 0u)
        {
            index = 0u;
            old_entry = table->entries[0u];
            if (old_entry.handle == 0u)
            {
                return status;
            }
        }
        else
        {
            return status;
        }
    }

    new_entry = old_entry;
    new_entry.handle = handle;
    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleSet.HandleValue",
                      (uint64_t)handle_value);
    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleSet.Index",
                      (uint64_t)index);
    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleSet.OldObjectHandle",
                      (uint64_t)old_entry.handle);
    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "GCHandleSet.NewObjectHandle",
                      (uint64_t)new_entry.handle);
    update_pinned_state(frame, &old_entry, &new_entry);
    table->entries[index] = new_entry;
    return zaclr_native_call_frame_set_void(&frame);
}

struct zaclr_result zaclr_native_System_Runtime_InteropServices_GCHandle::InternalCompareExchange___STATIC__OBJECT__I__OBJECT__OBJECT(struct zaclr_native_call_frame& frame)
{
    int64_t handle_value;
    zaclr_object_handle value;
    zaclr_object_handle old_value;
    uint32_t index;
    struct zaclr_gc_handle_entry old_entry;
    struct zaclr_gc_handle_entry new_entry;
    struct zaclr_handle_table* table = gc_handle_table(frame);
    struct zaclr_result status;

    if (table == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    status = zaclr_native_call_frame_arg_i8(&frame, 0u, &handle_value);
    if (status.status != ZACLR_STATUS_OK)
    {
        zaclr_object_handle boxed_handle = 0u;
        struct zaclr_boxed_value_desc* boxed_value = NULL;

        status = zaclr_native_call_frame_arg_object(&frame, 0u, &boxed_handle);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        boxed_value = frame.runtime != NULL
            ? zaclr_boxed_value_from_handle(&frame.runtime->heap, boxed_handle)
            : NULL;
        if (boxed_value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
        }

        if (boxed_value->value.kind == ZACLR_STACK_VALUE_I8)
        {
            handle_value = boxed_value->value.data.i8;
        }
        else if (boxed_value->value.kind == ZACLR_STACK_VALUE_I4)
        {
            handle_value = boxed_value->value.data.i4;
        }
        else
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
        }
    }

    status = zaclr_native_call_frame_arg_object(&frame, 1u, &value);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_object(&frame, 2u, &old_value);
    if (frame.runtime != NULL)
    {
        console_write("[ZACLR][gch-icx] handle_value=");
        console_write_hex64((uint64_t)handle_value);
        console_write(" arg1_obj=");
        console_write_hex64((uint64_t)value);
        console_write(" arg2_status=");
        console_write_dec((uint64_t)status.status);
        console_write(" arg2_cat=");
        console_write_dec((uint64_t)status.category);
        console_write("\n");
    }
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = get_entry(frame, handle_value, &index, &old_entry);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    if (old_entry.handle == old_value)
    {
        new_entry = old_entry;
        new_entry.handle = value;
        update_pinned_state(frame, &old_entry, &new_entry);
        table->entries[index] = new_entry;
    }

    return zaclr_native_call_frame_set_object(&frame, old_entry.handle);
}
