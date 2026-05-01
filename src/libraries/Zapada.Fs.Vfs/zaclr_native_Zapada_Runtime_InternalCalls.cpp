#include <kernel/zaclr/heap/zaclr_array.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>
#include <kernel/zaclr/loader/zaclr_assembly_source_vfs.h>
#include <kernel/zaclr/host/zaclr_host.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

#include "zaclr_native_Zapada_Runtime_InternalCalls.h"

struct zaclr_result zaclr_native_Zapada_Fs_Vfs_Runtime_InternalCalls::Publish___STATIC__I4__STRING__SZARRAY_U1(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* path_string;
    const struct zaclr_array_desc* image_array;
    const char* path;
    const uint8_t* data;
    uint32_t size;
    struct zaclr_result status;

    status = zaclr_native_call_frame_arg_string(&frame, 0u, &path_string);
    if (status.status != ZACLR_STATUS_OK || path_string == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    status = zaclr_native_call_frame_arg_array(&frame, 1u, &image_array);
    if (status.status != ZACLR_STATUS_OK || image_array == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -2);
    }

    if (zaclr_array_element_size(image_array) != 1u)
    {
        return zaclr_native_call_frame_set_i4(&frame, -3);
    }

    path = zaclr_string_ascii_chars(path_string);
    data = (const uint8_t*)zaclr_array_data_const(image_array);
    size = zaclr_array_length(image_array);
    if (path == NULL || path[0] != '/' || data == NULL || size == 0u)
    {
        return zaclr_native_call_frame_set_i4(&frame, -4);
    }

    status = zaclr_assembly_source_vfs_publish(path, data, (size_t)size);
    if (status.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_i4(&frame, -5);
    }

    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_Zapada_Fs_Vfs_Runtime_InternalCalls::PublishBegin___STATIC__I4__STRING__I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* path_string;
    const char* path;
    int32_t size;
    struct zaclr_result status;

    status = zaclr_native_call_frame_arg_string(&frame, 0u, &path_string);
    if (status.status != ZACLR_STATUS_OK || path_string == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &size);
    if (status.status != ZACLR_STATUS_OK || size <= 0)
    {
        return zaclr_native_call_frame_set_i4(&frame, -2);
    }

    path = zaclr_string_ascii_chars(path_string);
    if (path == NULL || path[0] != '/')
    {
        return zaclr_native_call_frame_set_i4(&frame, -3);
    }

    status = zaclr_assembly_source_vfs_publish_begin(path, (size_t)size);
    if (status.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_i4(&frame, -4);
    }

    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_Zapada_Fs_Vfs_Runtime_InternalCalls::PublishAppend___STATIC__I4__STRING__SZARRAY_U1__I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* path_string;
    const struct zaclr_array_desc* chunk_array;
    const char* path;
    const uint8_t* data;
    int32_t count;
    uint32_t array_size;
    struct zaclr_result status;

    status = zaclr_native_call_frame_arg_string(&frame, 0u, &path_string);
    if (status.status != ZACLR_STATUS_OK || path_string == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    status = zaclr_native_call_frame_arg_array(&frame, 1u, &chunk_array);
    if (status.status != ZACLR_STATUS_OK || chunk_array == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -2);
    }

    status = zaclr_native_call_frame_arg_i4(&frame, 2u, &count);
    if (status.status != ZACLR_STATUS_OK || count <= 0)
    {
        return zaclr_native_call_frame_set_i4(&frame, -3);
    }

    if (zaclr_array_element_size(chunk_array) != 1u)
    {
        return zaclr_native_call_frame_set_i4(&frame, -4);
    }

    array_size = zaclr_array_length(chunk_array);
    if ((uint32_t)count > array_size)
    {
        return zaclr_native_call_frame_set_i4(&frame, -5);
    }

    path = zaclr_string_ascii_chars(path_string);
    data = (const uint8_t*)zaclr_array_data_const(chunk_array);
    if (path == NULL || path[0] != '/' || data == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -6);
    }

    status = zaclr_assembly_source_vfs_publish_append(path, data, (size_t)count);
    if (status.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_i4(&frame, -7);
    }

    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_Zapada_Fs_Vfs_Runtime_InternalCalls::PublishEnd___STATIC__I4__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* path_string;
    const char* path;
    struct zaclr_result status;

    status = zaclr_native_call_frame_arg_string(&frame, 0u, &path_string);
    if (status.status != ZACLR_STATUS_OK || path_string == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    path = zaclr_string_ascii_chars(path_string);
    if (path == NULL || path[0] != '/')
    {
        return zaclr_native_call_frame_set_i4(&frame, -2);
    }

    status = zaclr_assembly_source_vfs_publish_end(path);
    if (status.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_i4(&frame, -3);
    }

    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_Zapada_Fs_Vfs_Runtime_InternalCalls::GetTickCount___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    if (frame.runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    return zaclr_native_call_frame_set_i4(&frame,
        (int32_t)(frame.runtime->state.host != NULL && frame.runtime->state.host->monotonic_ticks != NULL
            ? frame.runtime->state.host->monotonic_ticks()
            : 0u));
}
