#include "zaclr_native_System_GC.h"

#include <kernel/zaclr/heap/zaclr_gc.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>

namespace
{
    static const int32_t ZACLR_GC_MAX_GENERATION = 0;

    static struct zaclr_result set_many_zero_byrefs(struct zaclr_native_call_frame& frame,
                                                    uint32_t first_index,
                                                    uint32_t count)
    {
        for (uint32_t index = 0u; index < count; ++index)
        {
            struct zaclr_result result = zaclr_native_call_frame_store_byref_i8(&frame, first_index + index, 0);
            if (result.status != ZACLR_STATUS_OK)
            {
                result = zaclr_native_call_frame_store_byref_i4(&frame, first_index + index, 0);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
            }
        }

        return zaclr_native_call_frame_set_void(&frame);
    }

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

struct zaclr_result zaclr_native_System_GC::GCInterface_Collect___STATIC__VOID__I4__I4__BOOLEAN(struct zaclr_native_call_frame& frame)
{
    return _Collect___STATIC__VOID__I4__I4__BOOLEAN(frame);
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

struct zaclr_result zaclr_native_System_GC::GCInterface_WaitForPendingFinalizers___STATIC__VOID(struct zaclr_native_call_frame& frame)
{
    return WaitForPendingFinalizers___STATIC__VOID(frame);
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

struct zaclr_result zaclr_native_System_GC::GCInterface_ReRegisterForFinalize___STATIC__VOID__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle handle = 0u;
    struct zaclr_result status = zaclr_native_call_frame_load_byref_object(&frame, 0u, &handle);
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

struct zaclr_result zaclr_native_System_GC::GetMemoryInfo___STATIC__VOID__I4__I4__I4__I8__I8__I8__I8__I8__I8__I8__I8__I8__I4__I8__I8(struct zaclr_native_call_frame& frame)
{
    return set_many_zero_byrefs(frame, 3u, 11u);
}

struct zaclr_result zaclr_native_System_GC::GetSegmentSize___STATIC__I8(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i8(&frame, 0);
}

struct zaclr_result zaclr_native_System_GC::GetLastGCPercentTimeInGC___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_System_GC::GetGenerationSize___STATIC__I8__I4(struct zaclr_native_call_frame& frame)
{
    int32_t generation;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &generation);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    (void)generation;
    return zaclr_native_call_frame_set_i8(&frame, (int64_t)zaclr_heap_allocated_bytes(&frame.runtime->heap));
}

struct zaclr_result zaclr_native_System_GC::GetAllocatedBytesForCurrentThread___STATIC__I8(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i8(&frame, (int64_t)zaclr_heap_allocated_bytes(&frame.runtime->heap));
}

struct zaclr_result zaclr_native_System_GC::GetTotalAllocatedBytesApproximate___STATIC__I8(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i8(&frame, (int64_t)zaclr_heap_allocated_bytes(&frame.runtime->heap));
}

struct zaclr_result zaclr_native_System_GC::GetMemoryLoad___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_System_GC::_RegisterForFullGCNotification___STATIC__BOOLEAN__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t max_generation_threshold;
    int32_t large_object_heap_threshold;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &max_generation_threshold);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &large_object_heap_threshold);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    (void)max_generation_threshold;
    (void)large_object_heap_threshold;
    return zaclr_native_call_frame_set_bool(&frame, false);
}

struct zaclr_result zaclr_native_System_GC::_CancelFullGCNotification___STATIC__BOOLEAN(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_bool(&frame, false);
}

struct zaclr_result zaclr_native_System_GC::GCInterface_GetNextFinalizableObject___STATIC__OBJECT(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_object(&frame, 0u);
}

struct zaclr_result zaclr_native_System_GC::GCInterface_AddMemoryPressure___STATIC__VOID__I8(struct zaclr_native_call_frame& frame)
{
    int64_t bytes_allocated;
    struct zaclr_result status = zaclr_native_call_frame_arg_i8(&frame, 0u, &bytes_allocated);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    (void)bytes_allocated;
    return zaclr_native_call_frame_set_void(&frame);
}

struct zaclr_result zaclr_native_System_GC::GCInterface_RemoveMemoryPressure___STATIC__VOID__I8(struct zaclr_native_call_frame& frame)
{
    int64_t bytes_allocated;
    struct zaclr_result status = zaclr_native_call_frame_arg_i8(&frame, 0u, &bytes_allocated);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    (void)bytes_allocated;
    return zaclr_native_call_frame_set_void(&frame);
}

struct zaclr_result zaclr_native_System_GC::GCInterface_StartNoGCRegion___STATIC__I4__I8__BOOLEAN__I8__I8(struct zaclr_native_call_frame& frame)
{
    int64_t total_size;
    bool has_loh_size;
    int64_t loh_size;
    int64_t disallow_full_blocking_gc;
    struct zaclr_result status = zaclr_native_call_frame_arg_i8(&frame, 0u, &total_size);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_bool(&frame, 1u, &has_loh_size);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_i8(&frame, 2u, &loh_size);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_i8(&frame, 3u, &disallow_full_blocking_gc);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    (void)total_size;
    (void)has_loh_size;
    (void)loh_size;
    (void)disallow_full_blocking_gc;
    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_System_GC::GCInterface_EndNoGCRegion___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_System_GC::GCInterface_RegisterFrozenSegment___STATIC__I__I__I(struct zaclr_native_call_frame& frame)
{
    int64_t segment;
    int32_t size;
    struct zaclr_result status = zaclr_native_call_frame_arg_i8(&frame, 0u, &segment);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &size);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    (void)segment;
    (void)size;
    return zaclr_native_call_frame_set_i8(&frame, 0);
}

struct zaclr_result zaclr_native_System_GC::GCInterface_UnregisterFrozenSegment___STATIC__VOID__I(struct zaclr_native_call_frame& frame)
{
    int64_t segment;
    struct zaclr_result status = zaclr_native_call_frame_arg_i8(&frame, 0u, &segment);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    (void)segment;
    return zaclr_native_call_frame_set_void(&frame);
}

struct zaclr_result zaclr_native_System_GC::GCInterface_AllocateNewArray___STATIC__OBJECT__I__I4__I4(struct zaclr_native_call_frame& frame)
{
    int64_t type_handle;
    int32_t length;
    int32_t flags;
    struct zaclr_result status = zaclr_native_call_frame_arg_i8(&frame, 0u, &type_handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &length);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_i4(&frame, 2u, &flags);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    (void)type_handle;
    (void)length;
    (void)flags;
    return zaclr_native_call_frame_set_object(&frame, 0u);
}

struct zaclr_result zaclr_native_System_GC::GCInterface_GetTotalAllocatedBytesPrecise___STATIC__I8(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i8(&frame, (int64_t)zaclr_heap_allocated_bytes(&frame.runtime->heap));
}

struct zaclr_result zaclr_native_System_GC::GCInterface_GetTotalMemory___STATIC__I8__BOOLEAN(struct zaclr_native_call_frame& frame)
{
    return GetTotalMemory___STATIC__I8__BOOLEAN(frame);
}

struct zaclr_result zaclr_native_System_GC::GCInterface_WaitForFullGCApproach___STATIC__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t milliseconds_timeout;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &milliseconds_timeout);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    (void)milliseconds_timeout;
    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_System_GC::GCInterface_WaitForFullGCComplete___STATIC__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t milliseconds_timeout;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &milliseconds_timeout);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    (void)milliseconds_timeout;
    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_System_GC::Pin___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame)
{
    return set_pin_state(frame, 1u);
}

struct zaclr_result zaclr_native_System_GC::Unpin___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame)
{
    return set_pin_state(frame, 0u);
}
