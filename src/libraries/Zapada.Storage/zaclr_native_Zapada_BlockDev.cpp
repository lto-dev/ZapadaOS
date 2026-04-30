#include "zaclr_native_Zapada_BlockDev.h"

#include <kernel/support/kernel_memory.h>
#include <kernel/drivers/block.h>
#include <kernel/zaclr/heap/zaclr_array.h>

extern "C" int32_t native_read_sector(int64_t lba, int32_t count, void *arr_obj);
extern "C" int32_t native_write_sector(int64_t lba, int32_t count, void *arr_obj);
extern "C" int32_t native_read_sector_device(int32_t device_index, int64_t lba, int32_t count, void *arr_obj);
extern "C" int32_t native_write_sector_device(int32_t device_index, int64_t lba, int32_t count, void *arr_obj);

namespace
{
    static const uint32_t blockdev_transfer_header_size = 24u;

    static struct zaclr_result invoke_blockdev_transfer(struct zaclr_native_call_frame& frame,
                                                        int32_t (*transfer)(int64_t, int32_t, void*),
                                                        bool copy_back)
    {
        int64_t lba;
        int32_t count;
        const struct zaclr_array_desc* array;
        uint8_t* legacy_array;
        int32_t rc;
        uint32_t payload_size;
        struct zaclr_result status = zaclr_native_call_frame_arg_i8(&frame, 0u, &lba);
        if (status.status != ZACLR_STATUS_OK) return status;
        status = zaclr_native_call_frame_arg_i4(&frame, 1u, &count);
        if (status.status != ZACLR_STATUS_OK) return status;
        status = zaclr_native_call_frame_arg_array(&frame, 2u, &array);
        if (status.status != ZACLR_STATUS_OK) return status;
        if (array == NULL || zaclr_array_element_size(array) != 4u || count <= 0) return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        payload_size = (uint32_t)count * 512u;
        legacy_array = (uint8_t*)kernel_alloc(blockdev_transfer_header_size + payload_size);
        if (legacy_array == NULL) return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
        kernel_memset(legacy_array, 0, blockdev_transfer_header_size + payload_size);
        if (!copy_back)
        {
            kernel_memcpy(legacy_array + blockdev_transfer_header_size, zaclr_array_data_const(array), payload_size);
        }
        rc = transfer(lba, count, legacy_array);
        if (copy_back && rc == 0)
        {
            kernel_memcpy(zaclr_array_data((struct zaclr_array_desc*)array), legacy_array + blockdev_transfer_header_size, payload_size);
        }
        kernel_free(legacy_array);
        return zaclr_native_call_frame_set_i4(&frame, rc);
    }

    static struct zaclr_result invoke_blockdev_transfer_for_device(struct zaclr_native_call_frame& frame,
                                                                   int32_t (*transfer)(int32_t, int64_t, int32_t, void*),
                                                                   bool copy_back)
    {
        int32_t device_index;
        int64_t lba;
        int32_t count;
        const struct zaclr_array_desc* array;
        uint8_t* legacy_array;
        int32_t rc;
        uint32_t payload_size;
        struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &device_index);
        if (status.status != ZACLR_STATUS_OK) return status;
        status = zaclr_native_call_frame_arg_i8(&frame, 1u, &lba);
        if (status.status != ZACLR_STATUS_OK) return status;
        status = zaclr_native_call_frame_arg_i4(&frame, 2u, &count);
        if (status.status != ZACLR_STATUS_OK) return status;
        status = zaclr_native_call_frame_arg_array(&frame, 3u, &array);
        if (status.status != ZACLR_STATUS_OK) return status;
        if (array == NULL || zaclr_array_element_size(array) != 4u || count <= 0) return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        payload_size = (uint32_t)count * 512u;
        legacy_array = (uint8_t*)kernel_alloc(blockdev_transfer_header_size + payload_size);
        if (legacy_array == NULL) return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
        kernel_memset(legacy_array, 0, blockdev_transfer_header_size + payload_size);
        if (!copy_back)
        {
            kernel_memcpy(legacy_array + blockdev_transfer_header_size, zaclr_array_data_const(array), payload_size);
        }
        rc = transfer(device_index, lba, count, legacy_array);
        if (copy_back && rc == 0)
        {
            kernel_memcpy(zaclr_array_data((struct zaclr_array_desc*)array), legacy_array + blockdev_transfer_header_size, payload_size);
        }
        kernel_free(legacy_array);
        return zaclr_native_call_frame_set_i4(&frame, rc);
    }
}

struct zaclr_result zaclr_native_Zapada_BlockDev::SectorCount___STATIC__I8(struct zaclr_native_call_frame& frame)
{
    if (g_block_vda.present == 0)
    {
        return zaclr_native_call_frame_set_i8(&frame, 0);
    }

    return zaclr_native_call_frame_set_i8(&frame, (int64_t)g_block_vda.sector_count);
}

struct zaclr_result zaclr_native_Zapada_BlockDev::DeviceCount___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)g_block_device_count);
}

struct zaclr_result zaclr_native_Zapada_BlockDev::SectorCountForDevice___STATIC__I8__I4(struct zaclr_native_call_frame& frame)
{
    int32_t device_index;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &device_index);
    if (status.status != ZACLR_STATUS_OK) return status;
    if (device_index < 0 || (uint32_t)device_index >= g_block_device_count ||
        g_block_devices[device_index].present == 0)
    {
        return zaclr_native_call_frame_set_i8(&frame, 0);
    }

    return zaclr_native_call_frame_set_i8(&frame, (int64_t)g_block_devices[device_index].sector_count);
}

struct zaclr_result zaclr_native_Zapada_BlockDev::ReadSector___STATIC__I4__I8__I4__SZARRAY_I4(struct zaclr_native_call_frame& frame)
{
    return invoke_blockdev_transfer(frame, &native_read_sector, true);
}

struct zaclr_result zaclr_native_Zapada_BlockDev::WriteSector___STATIC__I4__I8__I4__SZARRAY_I4(struct zaclr_native_call_frame& frame)
{
    return invoke_blockdev_transfer(frame, &native_write_sector, false);
}

struct zaclr_result zaclr_native_Zapada_BlockDev::ReadSectorForDevice___STATIC__I4__I4__I8__I4__SZARRAY_I4(struct zaclr_native_call_frame& frame)
{
    return invoke_blockdev_transfer_for_device(frame, &native_read_sector_device, true);
}

struct zaclr_result zaclr_native_Zapada_BlockDev::WriteSectorForDevice___STATIC__I4__I4__I8__I4__SZARRAY_I4(struct zaclr_native_call_frame& frame)
{
    return invoke_blockdev_transfer_for_device(frame, &native_write_sector_device, false);
}
