#include "zaclr_native_Zapada_Runtime_InternalCalls.h"

#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/heap/zaclr_gc.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
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

struct zaclr_result zaclr_native_Zapada_Conformance_Runtime_InternalCalls::Write___STATIC__VOID__STRING(struct zaclr_native_call_frame& frame)
{
    const char* text;
    zaclr_object_handle string_handle;
    struct zaclr_result status;

    if (frame.runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    status = zaclr_native_call_frame_arg_object(&frame, 0u, &string_handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    if (string_handle == 0u)
    {
        return zaclr_native_call_frame_set_void(&frame);
    }

    text = zaclr_string_ascii_chars_from_handle(&frame.runtime->heap, string_handle);
    if (text == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    console_write(text);
    return zaclr_native_call_frame_set_void(&frame);
}

struct zaclr_result zaclr_native_Zapada_Conformance_Runtime_InternalCalls::WriteInt___STATIC__VOID__I4(struct zaclr_native_call_frame& frame)
{
    int32_t value;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &value);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    console_write_dec((int64_t)value);
    return zaclr_native_call_frame_set_void(&frame);
}

struct zaclr_result zaclr_native_Zapada_Conformance_Runtime_InternalCalls::GcCollect___STATIC__VOID(struct zaclr_native_call_frame& frame)
{
    struct zaclr_result status = zaclr_gc_collect(frame.runtime);
    return status.status == ZACLR_STATUS_OK ? zaclr_native_call_frame_set_void(&frame) : status;
}

struct zaclr_result zaclr_native_Zapada_Conformance_Runtime_InternalCalls::GcGetTotalMemory___STATIC__I8__BOOLEAN(struct zaclr_native_call_frame& frame)
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

struct zaclr_result zaclr_native_Zapada_Conformance_Runtime_InternalCalls::GcGetFreeBytes___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    const uint32_t allocated = zaclr_heap_allocated_bytes(&frame.runtime->heap);
    const uint32_t threshold = frame.runtime != NULL ? frame.runtime->heap.collection_threshold_bytes : 0u;
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)(threshold > allocated ? (threshold - allocated) : 0u));
}

struct zaclr_result zaclr_native_Zapada_Conformance_Runtime_InternalCalls::GcPin___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame)
{
    return set_pin_state(frame, 1u);
}

struct zaclr_result zaclr_native_Zapada_Conformance_Runtime_InternalCalls::GcUnpin___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame)
{
    return set_pin_state(frame, 0u);
}
