#include "zaclr_native_Zapada_BlockDev.h"

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/heap/zaclr_array.h>

extern "C" int32_t native_read_sector(int64_t lba, int32_t count, void *arr_obj);
extern "C" int32_t native_write_sector(int64_t lba, int32_t count, void *arr_obj);

namespace
{
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
        legacy_array = (uint8_t*)kernel_alloc(16u + payload_size);
        if (legacy_array == NULL) return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
        kernel_memset(legacy_array, 0, 16u + payload_size);
        if (!copy_back)
        {
            kernel_memcpy(legacy_array + 16u, zaclr_array_data_const(array), payload_size);
        }
        rc = transfer(lba, count, legacy_array);
        if (copy_back && rc == 0)
        {
            kernel_memcpy(zaclr_array_data((struct zaclr_array_desc*)array), legacy_array + 16u, payload_size);
        }
        kernel_free(legacy_array);
        return zaclr_native_call_frame_set_i4(&frame, rc);
    }
}

struct zaclr_result zaclr_native_Zapada_BlockDev::ReadSector___STATIC__I4__I8__I4__SZARRAY_I4(struct zaclr_native_call_frame& frame)
{
    return invoke_blockdev_transfer(frame, &native_read_sector, true);
}

struct zaclr_result zaclr_native_Zapada_BlockDev::WriteSector___STATIC__I4__I8__I4__SZARRAY_I4(struct zaclr_native_call_frame& frame)
{
    return invoke_blockdev_transfer(frame, &native_write_sector, false);
}
