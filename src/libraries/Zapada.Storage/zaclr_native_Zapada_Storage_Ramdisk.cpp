#include "zaclr_native_Zapada_Storage_Ramdisk.h"

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/heap/zaclr_array.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

extern "C" {
#include <kernel/initramfs/ramdisk.h>
}

namespace
{
    static int32_t lookup_index(const char* filename)
    {
        for (uint32_t i = 0u; i < ramdisk_file_count(); ++i)
        {
            ramdisk_file_t file = {};
            size_t index = 0u;
            if (filename == NULL || ramdisk_get_file(i, &file) != RAMDISK_OK || file.filename == NULL)
            {
                continue;
            }

            while (file.filename[index] != '\0' && filename[index] != '\0' && file.filename[index] == filename[index])
            {
                ++index;
            }

            if (file.filename[index] == '\0' && filename[index] == '\0')
            {
                return (int32_t)i;
            }
        }

        return -1;
    }
}

struct zaclr_result zaclr_native_Zapada_Storage_Ramdisk::FileCount___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)ramdisk_file_count());
}

struct zaclr_result zaclr_native_Zapada_Storage_Ramdisk::Lookup___STATIC__I4__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* filename;
    struct zaclr_result status = zaclr_native_call_frame_arg_string(&frame, 0u, &filename);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    return zaclr_native_call_frame_set_i4(&frame, lookup_index(filename != NULL ? zaclr_string_ascii_chars(filename) : NULL));
}

struct zaclr_result zaclr_native_Zapada_Storage_Ramdisk::Read___STATIC__I4__I4__SZARRAY_U1__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t file_index;
    int32_t offset;
    int32_t count;
    const struct zaclr_array_desc* buffer;
    ramdisk_file_t file = {};
    uint8_t* data;
    uint32_t to_copy;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &file_index);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_array(&frame, 1u, &buffer);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 2u, &offset);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 3u, &count);
    if (status.status != ZACLR_STATUS_OK) return status;
    if (file_index < 0 || buffer == NULL || offset < 0 || count < 0 || zaclr_array_element_size(buffer) != 1u) return zaclr_native_call_frame_set_i4(&frame, -1);
    if (ramdisk_get_file((uint32_t)file_index, &file) != RAMDISK_OK) return zaclr_native_call_frame_set_i4(&frame, -1);
    if ((uint32_t)offset > zaclr_array_length(buffer)) return zaclr_native_call_frame_set_i4(&frame, -1);
    to_copy = (uint32_t)count;
    if (to_copy > file.size) to_copy = file.size;
    if ((uint32_t)offset + to_copy > zaclr_array_length(buffer)) to_copy = zaclr_array_length(buffer) - (uint32_t)offset;
    data = (uint8_t*)zaclr_array_data((struct zaclr_array_desc*)buffer);
    kernel_memcpy(data + (uint32_t)offset, file.data, to_copy);
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)to_copy);
}

struct zaclr_result zaclr_native_Zapada_Storage_Ramdisk::GetFileName___STATIC__STRING__I4(struct zaclr_native_call_frame& frame)
{
    int32_t file_index;
    ramdisk_file_t file = {};
    zaclr_object_handle handle;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &file_index);
    if (status.status != ZACLR_STATUS_OK) return status;
    if (file_index < 0 || ramdisk_get_file((uint32_t)file_index, &file) != RAMDISK_OK) return zaclr_native_call_frame_set_object(&frame, 0u);
    {
        uint32_t length = 0u;
        while (file.filename[length] != '\0')
        {
            ++length;
        }
        status = zaclr_string_allocate_ascii_handle(&frame.runtime->heap, file.filename, length, &handle);
    }
    return status.status == ZACLR_STATUS_OK ? zaclr_native_call_frame_set_string(&frame, handle) : status;
}

struct zaclr_result zaclr_native_Zapada_Storage_Ramdisk::GetFileSize___STATIC__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t file_index;
    ramdisk_file_t file = {};
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &file_index);
    if (status.status != ZACLR_STATUS_OK) return status;
    if (file_index < 0 || ramdisk_get_file((uint32_t)file_index, &file) != RAMDISK_OK) return zaclr_native_call_frame_set_i4(&frame, 0);
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)file.size);
}
