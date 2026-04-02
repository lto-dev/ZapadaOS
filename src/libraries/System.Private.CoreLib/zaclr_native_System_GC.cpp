#include "zaclr_native_System_GC.h"

#include <kernel/zaclr/heap/zaclr_gc.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>

namespace
{
    static const int32_t ZACLR_GC_MAX_GENERATION = 0;

    static struct zaclr_result set_pin_state(struct zaclr_native_call_frame& frame, uint32_t pinned)
    {
        zaclr_object_handle handle;
        struct zaclr_object_desc* object;
        struct zaclr_result status = zaclr_native_call_frame_arg_object(&frame, 0u, &handle);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        if (handle == 0u)
        {
            return zaclr_native_call_frame_set_void(&frame);
        }

        object = zaclr_heap_get_object(&frame.runtime->heap, handle);
        if (object == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
        }

        if (pinned != 0u)
        {
            object->gc_state = (uint8_t)(object->gc_state | ZACLR_OBJECT_GC_STATE_PINNED);
        }
        else
        {
            object->gc_state = (uint8_t)(object->gc_state & ~ZACLR_OBJECT_GC_STATE_PINNED);
        }

        return zaclr_native_call_frame_set_void(&frame);
    }
}

struct zaclr_result zaclr_native_System_GC::Collect___STATIC__VOID(struct zaclr_native_call_frame& frame)
{
    struct zaclr_result status = zaclr_gc_collect(frame.runtime);
    return status.status == ZACLR_STATUS_OK ? zaclr_native_call_frame_set_void(&frame) : status;
}

struct zaclr_result zaclr_native_System_GC::_Collect___STATIC__VOID__I4__I4__BOOLEAN(struct zaclr_native_call_frame& frame)
{
    int32_t generation;
    int32_t mode;
    bool low_memory_pressure;

    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &generation);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &mode);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_bool(&frame, 2u, &low_memory_pressure);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    (void)generation;
    (void)mode;
    (void)low_memory_pressure;

    status = zaclr_gc_collect(frame.runtime);
    return status.status == ZACLR_STATUS_OK ? zaclr_native_call_frame_set_void(&frame) : status;
}

struct zaclr_result zaclr_native_System_GC::GetMaxGeneration___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i4(&frame, ZACLR_GC_MAX_GENERATION);
}

struct zaclr_result zaclr_native_System_GC::_CollectionCount___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t generation;
    int32_t get_special_gc_count;

    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &generation);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &get_special_gc_count);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    (void)generation;
    (void)get_special_gc_count;

    if (generation < 0)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    if (generation > ZACLR_GC_MAX_GENERATION)
    {
        return zaclr_native_call_frame_set_i4(&frame, 0);
    }

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)zaclr_gc_collection_count(frame.runtime));
}

struct zaclr_result zaclr_native_System_GC::GetGenerationInternal___STATIC__I4__OBJECT(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle handle;
    struct zaclr_result status = zaclr_native_call_frame_arg_object(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    if (handle == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    if (zaclr_heap_get_object(&frame.runtime->heap, handle) == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    return zaclr_native_call_frame_set_i4(&frame, ZACLR_GC_MAX_GENERATION);
}

struct zaclr_result zaclr_native_System_GC::WaitForPendingFinalizers___STATIC__VOID(struct zaclr_native_call_frame& frame)
{
    struct zaclr_result status = zaclr_gc_wait_for_pending_finalizers(frame.runtime);
    return status.status == ZACLR_STATUS_OK ? zaclr_native_call_frame_set_void(&frame) : status;
}

struct zaclr_result zaclr_native_System_GC::SuppressFinalizeInternal___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle handle;
    struct zaclr_result status = zaclr_native_call_frame_arg_object(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_gc_suppress_finalize(frame.runtime, handle);
    return status.status == ZACLR_STATUS_OK ? zaclr_native_call_frame_set_void(&frame) : status;
}

struct zaclr_result zaclr_native_System_GC::_ReRegisterForFinalize___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle handle;
    struct zaclr_result status = zaclr_native_call_frame_arg_object(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_gc_reregister_for_finalize(frame.runtime, handle);
    return status.status == ZACLR_STATUS_OK ? zaclr_native_call_frame_set_void(&frame) : status;
}

struct zaclr_result zaclr_native_System_GC::GetTotalMemory___STATIC__I8__BOOLEAN(struct zaclr_native_call_frame& frame)
{
    bool force_full_collection;
    struct zaclr_result status = zaclr_native_call_frame_arg_bool(&frame, 0u, &force_full_collection);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    if (force_full_collection)
    {
        status = zaclr_gc_collect(frame.runtime);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }
    }

    return zaclr_native_call_frame_set_i8(&frame, (int64_t)zaclr_heap_allocated_bytes(&frame.runtime->heap));
}

struct zaclr_result zaclr_native_System_GC::GetFreeBytes___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    const uint32_t allocated = zaclr_heap_allocated_bytes(&frame.runtime->heap);
    const uint32_t threshold = frame.runtime != NULL ? frame.runtime->heap.collection_threshold_bytes : 0u;
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)(threshold > allocated ? (threshold - allocated) : 0u));
}

struct zaclr_result zaclr_native_System_GC::Pin___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame)
{
    return set_pin_state(frame, 1u);
}

struct zaclr_result zaclr_native_System_GC::Unpin___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame)
{
    return set_pin_state(frame, 0u);
}
