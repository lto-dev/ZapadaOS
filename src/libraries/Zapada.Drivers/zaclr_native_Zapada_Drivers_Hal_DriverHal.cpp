#include "zaclr_native_Zapada_Drivers_Hal_DriverHal.h"

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/heap/zaclr_array.h>

extern "C" {
#include <kernel/ipc/ipc.h>
#ifdef ARCH_X86_64
#include <kernel/arch/x86_64/paging.h>
#include <kernel/arch/x86_64/pci.h>
#endif
}

namespace
{
    struct buffer_slot
    {
        void* ptr;
        uint32_t size;
    };

    struct mmio_region_slot
    {
        uint64_t base;
        uint32_t size;
        uint32_t device_handle;
        uint8_t bar_index;
    };

    static const uint32_t max_buffer_slots = 16u;
    static buffer_slot s_buffer_slots[max_buffer_slots];

    static const int32_t driver_hal_status_invalid = -1;
    static const int32_t driver_hal_status_unsupported = -2;

    static const uint32_t max_mmio_region_slots = 16u;
    static const uint32_t default_mmio_region_size = 0x1000u;
    static mmio_region_slot s_mmio_region_slots[max_mmio_region_slots];

    static int32_t alloc_buffer_slot(void* ptr, uint32_t size)
    {
        for (uint32_t i = 0u; i < max_buffer_slots; i++)
        {
            if (s_buffer_slots[i].ptr == nullptr)
            {
                s_buffer_slots[i].ptr = ptr;
                s_buffer_slots[i].size = size;
                return (int32_t)(i + 1u);
            }
        }

        return -1;
    }

    static buffer_slot* get_buffer_slot(int32_t handle)
    {
        if (handle <= 0 || (uint32_t)handle > max_buffer_slots)
        {
            return nullptr;
        }

        buffer_slot* slot = &s_buffer_slots[(uint32_t)handle - 1u];
        return slot->ptr != nullptr ? slot : nullptr;
    }

    static int32_t alloc_mmio_region_slot(uint64_t base, uint32_t size, uint32_t device_handle, uint8_t bar_index)
    {
        if (base == 0u || size == 0u)
        {
            return driver_hal_status_invalid;
        }

        for (uint32_t i = 0u; i < max_mmio_region_slots; i++)
        {
            if (s_mmio_region_slots[i].base == 0u)
            {
                s_mmio_region_slots[i].base = base;
                s_mmio_region_slots[i].size = size;
                s_mmio_region_slots[i].device_handle = device_handle;
                s_mmio_region_slots[i].bar_index = bar_index;
                return (int32_t)(i + 1u);
            }
        }

        return driver_hal_status_invalid;
    }

    static mmio_region_slot* get_mmio_region_slot(int32_t handle)
    {
        if (handle <= 0 || (uint32_t)handle > max_mmio_region_slots)
        {
            return nullptr;
        }

        mmio_region_slot* slot = &s_mmio_region_slots[(uint32_t)handle - 1u];
        return slot->base != 0u ? slot : nullptr;
    }

    static int validate_mmio_access(mmio_region_slot* slot, int32_t offset)
    {
        if (slot == nullptr || offset < 0 || ((uint32_t)offset & 3u) != 0u)
        {
            return 0;
        }

        uint32_t access_offset = (uint32_t)offset;
        if (access_offset > slot->size || slot->size - access_offset < 4u)
        {
            return 0;
        }

        return 1;
    }

    static void write_payload_u64(uint8_t* payload, uint32_t offset, uint64_t value)
    {
        for (uint32_t i = 0u; i < 8u; i++)
        {
            payload[offset + i] = (uint8_t)((value >> (i * 8u)) & 0xFFu);
        }
    }

    static void write_u32(uint8_t* payload, uint32_t offset, uint32_t value)
    {
        payload[offset + 0u] = (uint8_t)(value & 0xFFu);
        payload[offset + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
        payload[offset + 2u] = (uint8_t)((value >> 16u) & 0xFFu);
        payload[offset + 3u] = (uint8_t)((value >> 24u) & 0xFFu);
    }

#ifdef ARCH_X86_64
    static int pci_device_exists_local(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t* id_out)
    {
        uint32_t id = pci_cfg_read32(bus, dev, fn, 0x00u);
        if (id == 0xFFFFFFFFu || id == 0u)
        {
            return 0;
        }

        if (id_out != nullptr)
        {
            *id_out = id;
        }

        return 1;
    }

    static uint8_t pci_cfg_read8_local(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
    {
        uint32_t value = pci_cfg_read32(bus, dev, fn, (uint8_t)(reg & 0xFCu));
        return (uint8_t)((value >> ((reg & 3u) * 8u)) & 0xFFu);
    }

    static uint8_t pci_function_limit_local(uint8_t bus, uint8_t dev)
    {
        uint32_t id;
        if (!pci_device_exists_local(bus, dev, 0u, &id))
        {
            return 0u;
        }

        uint8_t header_type = pci_cfg_read8_local(bus, dev, 0u, 0x0Eu);
        return (header_type & 0x80u) != 0u ? 8u : 1u;
    }

    static uint64_t pci_bar_mmio_base_local(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t bar_index, int32_t* status_out)
    {
        uint8_t reg = (uint8_t)(0x10u + (bar_index * 4u));
        uint32_t low = pci_cfg_read32(bus, dev, fn, reg);
        if (low == 0u || low == 0xFFFFFFFFu)
        {
            if (status_out != nullptr) *status_out = driver_hal_status_invalid;
            return 0u;
        }

        if ((low & 1u) != 0u)
        {
            if (status_out != nullptr) *status_out = driver_hal_status_unsupported;
            return 0u;
        }

        uint32_t type = (low >> 1u) & 3u;
        if (type == 1u)
        {
            if (status_out != nullptr) *status_out = driver_hal_status_unsupported;
            return 0u;
        }

        if (type == 2u)
        {
            if (bar_index >= 5u)
            {
                if (status_out != nullptr) *status_out = driver_hal_status_invalid;
                return 0u;
            }

            uint32_t high = pci_cfg_read32(bus, dev, fn, (uint8_t)(reg + 4u));
            uint64_t base64 = ((uint64_t)high << 32u) | (uint64_t)(low & 0xFFFFFFF0u);
            if (status_out != nullptr) *status_out = base64 != 0u ? 0 : driver_hal_status_invalid;
            return base64;
        }

        uint64_t base32 = (uint64_t)(low & 0xFFFFFFF0u);
        if (status_out != nullptr) *status_out = base32 != 0u ? 0 : driver_hal_status_invalid;
        return base32;
    }

#endif
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::CreateChannel___STATIC__I4(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)ipc_channel_create());
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::DestroyChannel___STATIC__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t handle;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK) return status;

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)ipc_channel_destroy((ipc_handle_t)handle));
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::TrySend___STATIC__I4__I4__I4__SZARRAY_U1__I4(struct zaclr_native_call_frame& frame)
{
    int32_t handle;
    int32_t type;
    int32_t length;
    const struct zaclr_array_desc* payload;
    ipc_message_t message;

    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &type);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_array(&frame, 2u, &payload);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 3u, &length);
    if (status.status != ZACLR_STATUS_OK) return status;

    if (payload == nullptr || zaclr_array_element_size(payload) != 1u || length < 0 || length > (int32_t)IPC_MSG_PAYLOAD_MAX)
    {
        return zaclr_native_call_frame_set_i4(&frame, IPC_ERR_INVAL);
    }

    if ((uint32_t)length > zaclr_array_length(payload))
    {
        return zaclr_native_call_frame_set_i4(&frame, IPC_ERR_INVAL);
    }

    message.type = (uint32_t)type;
    message.payload_len = (uint32_t)length;
    kernel_memset(message.payload, 0, IPC_MSG_PAYLOAD_MAX);
    kernel_memcpy(message.payload, zaclr_array_data_const(payload), (uint32_t)length);

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)ipc_trysend((ipc_handle_t)handle, &message));
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::TryReceive___STATIC__I4__I4__I4__SZARRAY_U1(struct zaclr_native_call_frame& frame)
{
    int32_t handle;
    int32_t type_filter;
    const struct zaclr_array_desc* payload;
    ipc_message_t message;

    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &type_filter);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_array(&frame, 2u, &payload);
    if (status.status != ZACLR_STATUS_OK) return status;

    if (payload == nullptr || zaclr_array_element_size(payload) != 1u || zaclr_array_length(payload) < IPC_MSG_PAYLOAD_MAX)
    {
        return zaclr_native_call_frame_set_i4(&frame, IPC_ERR_INVAL);
    }

    int32_t rc = (int32_t)ipc_tryrecv((ipc_handle_t)handle, (uint32_t)type_filter, &message);
    if (rc == IPC_OK)
    {
        struct zaclr_array_desc* mutable_payload = (struct zaclr_array_desc*)payload;
        uint8_t* data = (uint8_t*)zaclr_array_data(mutable_payload);
        kernel_memset(data, 0, zaclr_array_length(payload));
        write_payload_u64(data, 0u, (uint64_t)message.type);
        write_payload_u64(data, 8u, (uint64_t)message.payload_len);
        kernel_memcpy(data + 16u, message.payload, message.payload_len);
    }

    return zaclr_native_call_frame_set_i4(&frame, rc);
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::PciFindDevice___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t vendor;
    int32_t device;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &vendor);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &device);
    if (status.status != ZACLR_STATUS_OK) return status;

#ifdef ARCH_X86_64
    for (uint16_t bus = 0u; bus < 256u; bus++)
    {
        for (uint8_t dev = 0u; dev < 32u; dev++)
        {
            for (uint8_t fn = 0u; fn < 8u; fn++)
            {
                uint32_t id = pci_cfg_read32((uint8_t)bus, dev, fn, 0x00u);
                if (id == 0xFFFFFFFFu || id == 0u)
                {
                    if (fn == 0u)
                    {
                        break;
                    }
                    continue;
                }

                uint16_t found_vendor = (uint16_t)(id & 0xFFFFu);
                uint16_t found_device = (uint16_t)((id >> 16) & 0xFFFFu);
                if (found_vendor == (uint16_t)vendor && found_device == (uint16_t)device)
                {
                    int32_t handle = ((int32_t)bus << 16) | ((int32_t)dev << 8) | (int32_t)fn;
                    return zaclr_native_call_frame_set_i4(&frame, handle);
                }
            }
        }
    }
#endif

    return zaclr_native_call_frame_set_i4(&frame, -1);
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::PciDeviceCount___STATIC__I4(struct zaclr_native_call_frame& frame)
{
#ifdef ARCH_X86_64
    int32_t count = 0;
    for (uint16_t bus = 0u; bus < 256u; bus++)
    {
        for (uint8_t dev = 0u; dev < 32u; dev++)
        {
            uint8_t fn_limit = pci_function_limit_local((uint8_t)bus, dev);
            for (uint8_t fn = 0u; fn < fn_limit; fn++)
            {
                if (pci_device_exists_local((uint8_t)bus, dev, fn, nullptr))
                {
                    count++;
                }
            }
        }
    }

    return zaclr_native_call_frame_set_i4(&frame, count);
#else
    return zaclr_native_call_frame_set_i4(&frame, 0);
#endif
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::PciGetDeviceInfo___STATIC__I4__I4__SZARRAY_U1(struct zaclr_native_call_frame& frame)
{
    int32_t index;
    const struct zaclr_array_desc* buffer;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &index);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_array(&frame, 1u, &buffer);
    if (status.status != ZACLR_STATUS_OK) return status;

    if (index < 0 || buffer == nullptr || zaclr_array_element_size(buffer) != 1u || zaclr_array_length(buffer) < 32u)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

#ifdef ARCH_X86_64
    int32_t current = 0;
    for (uint16_t bus = 0u; bus < 256u; bus++)
    {
        for (uint8_t dev = 0u; dev < 32u; dev++)
        {
            uint8_t fn_limit = pci_function_limit_local((uint8_t)bus, dev);
            for (uint8_t fn = 0u; fn < fn_limit; fn++)
            {
                uint32_t id;
                if (!pci_device_exists_local((uint8_t)bus, dev, fn, &id))
                {
                    continue;
                }

                if (current != index)
                {
                    current++;
                    continue;
                }

                uint32_t class_reg = pci_cfg_read32((uint8_t)bus, dev, fn, 0x08u);
                uint8_t header_type = pci_cfg_read8_local((uint8_t)bus, dev, fn, 0x0Eu);
                uint32_t bar0 = pci_cfg_read32((uint8_t)bus, dev, fn, 0x10u);
                uint32_t handle = ((uint32_t)bus << 16u) | ((uint32_t)dev << 8u) | (uint32_t)fn;

                struct zaclr_array_desc* mutable_buffer = (struct zaclr_array_desc*)buffer;
                uint8_t* data = (uint8_t*)zaclr_array_data(mutable_buffer);
                kernel_memset(data, 0, zaclr_array_length(buffer));
                write_u32(data, 0u, handle);
                write_u32(data, 4u, id & 0xFFFFu);
                write_u32(data, 8u, (id >> 16u) & 0xFFFFu);
                write_u32(data, 12u, (class_reg >> 24u) & 0xFFu);
                write_u32(data, 16u, (class_reg >> 16u) & 0xFFu);
                write_u32(data, 20u, (class_reg >> 8u) & 0xFFu);
                write_u32(data, 24u, header_type);
                write_u32(data, 28u, bar0);
                return zaclr_native_call_frame_set_i4(&frame, 0);
            }
        }
    }
#endif

    return zaclr_native_call_frame_set_i4(&frame, -1);
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::PciReadConfig32___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t device_handle;
    int32_t reg;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &device_handle);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &reg);
    if (status.status != ZACLR_STATUS_OK) return status;

#ifdef ARCH_X86_64
    uint8_t bus = (uint8_t)((device_handle >> 16) & 0xFF);
    uint8_t dev = (uint8_t)((device_handle >> 8) & 0xFF);
    uint8_t fn = (uint8_t)(device_handle & 0xFF);
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)pci_cfg_read32(bus, dev, fn, (uint8_t)reg));
#else
    return zaclr_native_call_frame_set_i4(&frame, -1);
#endif
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::PciReadBar32___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t device_handle;
    int32_t bar_index;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &device_handle);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &bar_index);
    if (status.status != ZACLR_STATUS_OK) return status;

    if (bar_index < 0 || bar_index > 5)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

#ifdef ARCH_X86_64
    uint8_t bus = (uint8_t)((device_handle >> 16) & 0xFF);
    uint8_t dev = (uint8_t)((device_handle >> 8) & 0xFF);
    uint8_t fn = (uint8_t)(device_handle & 0xFF);
    uint8_t reg = (uint8_t)(0x10 + (bar_index * 4));
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)pci_cfg_read32(bus, dev, fn, reg));
#else
    return zaclr_native_call_frame_set_i4(&frame, -1);
#endif
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::PciOpenBar___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t device_handle;
    int32_t bar_index;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &device_handle);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &bar_index);
    if (status.status != ZACLR_STATUS_OK) return status;

    if (bar_index < 0 || bar_index > 5)
    {
        return zaclr_native_call_frame_set_i4(&frame, driver_hal_status_invalid);
    }

#ifdef ARCH_X86_64
    uint8_t bus = (uint8_t)((device_handle >> 16) & 0xFF);
    uint8_t dev = (uint8_t)((device_handle >> 8) & 0xFF);
    uint8_t fn = (uint8_t)(device_handle & 0xFF);
    if (!pci_device_exists_local(bus, dev, fn, nullptr))
    {
        return zaclr_native_call_frame_set_i4(&frame, driver_hal_status_invalid);
    }

    int32_t bar_status = 0;
    uint64_t base = pci_bar_mmio_base_local(bus, dev, fn, (uint8_t)bar_index, &bar_status);
    if (base == 0u)
    {
        return zaclr_native_call_frame_set_i4(&frame, bar_status != 0 ? bar_status : driver_hal_status_invalid);
    }

    int32_t handle = alloc_mmio_region_slot(base, default_mmio_region_size, (uint32_t)device_handle, (uint8_t)bar_index);
    if (handle > 0 && x86_paging_identity_map_mmio_2m(base, default_mmio_region_size) != 0)
    {
        mmio_region_slot* slot = get_mmio_region_slot(handle);
        if (slot != nullptr)
        {
            slot->base = 0u;
            slot->size = 0u;
            slot->device_handle = 0u;
            slot->bar_index = 0u;
        }

        return zaclr_native_call_frame_set_i4(&frame, driver_hal_status_invalid);
    }

    return zaclr_native_call_frame_set_i4(&frame, handle);
#else
    return zaclr_native_call_frame_set_i4(&frame, driver_hal_status_unsupported);
#endif
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::MmioRead32___STATIC__I4__I8__I4(struct zaclr_native_call_frame& frame)
{
    int64_t base;
    int32_t offset;
    struct zaclr_result status = zaclr_native_call_frame_arg_i8(&frame, 0u, &base);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &offset);
    if (status.status != ZACLR_STATUS_OK) return status;

    if (base <= 0 || offset < 0)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    volatile uint32_t* reg = (volatile uint32_t*)(uintptr_t)((uint64_t)base + (uint32_t)offset);
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)(*reg));
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::MmioWrite32___STATIC__I4__I8__I4__I4(struct zaclr_native_call_frame& frame)
{
    int64_t base;
    int32_t offset;
    int32_t value;
    struct zaclr_result status = zaclr_native_call_frame_arg_i8(&frame, 0u, &base);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &offset);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 2u, &value);
    if (status.status != ZACLR_STATUS_OK) return status;

    if (base <= 0 || offset < 0)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    volatile uint32_t* reg = (volatile uint32_t*)(uintptr_t)((uint64_t)base + (uint32_t)offset);
    *reg = (uint32_t)value;
    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::MmioRegionSize___STATIC__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t handle;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK) return status;

    mmio_region_slot* slot = get_mmio_region_slot(handle);
    return zaclr_native_call_frame_set_i4(&frame, slot != nullptr ? (int32_t)slot->size : driver_hal_status_invalid);
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::MmioRegionRead32___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t handle;
    int32_t offset;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &offset);
    if (status.status != ZACLR_STATUS_OK) return status;

    mmio_region_slot* slot = get_mmio_region_slot(handle);
    if (!validate_mmio_access(slot, offset))
    {
        return zaclr_native_call_frame_set_i4(&frame, driver_hal_status_invalid);
    }

    volatile uint32_t* reg = (volatile uint32_t*)(uintptr_t)(slot->base + (uint32_t)offset);
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)(*reg));
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::MmioRegionWrite32___STATIC__I4__I4__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t handle;
    int32_t offset;
    int32_t value;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 1u, &offset);
    if (status.status != ZACLR_STATUS_OK) return status;
    status = zaclr_native_call_frame_arg_i4(&frame, 2u, &value);
    if (status.status != ZACLR_STATUS_OK) return status;

    mmio_region_slot* slot = get_mmio_region_slot(handle);
    if (!validate_mmio_access(slot, offset))
    {
        return zaclr_native_call_frame_set_i4(&frame, driver_hal_status_invalid);
    }

    volatile uint32_t* reg = (volatile uint32_t*)(uintptr_t)(slot->base + (uint32_t)offset);
    *reg = (uint32_t)value;
    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::CloseMmioRegion___STATIC__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t handle;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK) return status;

    mmio_region_slot* slot = get_mmio_region_slot(handle);
    if (slot == nullptr)
    {
        return zaclr_native_call_frame_set_i4(&frame, driver_hal_status_invalid);
    }

    slot->base = 0u;
    slot->size = 0u;
    slot->device_handle = 0u;
    slot->bar_index = 0u;
    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::AllocBuffer___STATIC__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t size;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &size);
    if (status.status != ZACLR_STATUS_OK) return status;

    if (size <= 0)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    void* ptr = kernel_alloc((size_t)size);
    if (ptr == nullptr)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    kernel_memset(ptr, 0, (uint32_t)size);
    int32_t handle = alloc_buffer_slot(ptr, (uint32_t)size);
    if (handle < 0)
    {
        kernel_free(ptr);
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    return zaclr_native_call_frame_set_i4(&frame, handle);
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::FreeBuffer___STATIC__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t handle;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK) return status;

    buffer_slot* slot = get_buffer_slot(handle);
    if (slot == nullptr)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    kernel_free(slot->ptr);
    slot->ptr = nullptr;
    slot->size = 0u;
    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_Zapada_Drivers_Hal_DriverHal::BufferSize___STATIC__I4__I4(struct zaclr_native_call_frame& frame)
{
    int32_t handle;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &handle);
    if (status.status != ZACLR_STATUS_OK) return status;

    buffer_slot* slot = get_buffer_slot(handle);
    return zaclr_native_call_frame_set_i4(&frame, slot != nullptr ? (int32_t)slot->size : -1);
}
