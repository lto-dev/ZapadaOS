/*
 * Zapada - src/kernel/arch/aarch64/virtio_mmio.c
 *
 * AArch64 VirtIO-MMIO platform device probe.
 *
 * Scans the fixed-address MMIO bus used by the QEMU virt machine.  Each slot is
 * identified by the Magic register.  The first slot whose DeviceID equals
 * VIRTIO_DEVICE_ID_BLOCK (0x02) is returned as the active block device.
 *
 * All MMIO reads use volatile 32-bit pointer dereferences — required by
 * -mstrict-align on AArch64.  No larger-than-32-bit atomic accesses.
 */

#include <kernel/arch/aarch64/virtio_mmio.h>
#include <kernel/types.h>

/*
 * mmio_read32 - Read a 32-bit MMIO register.
 * base:   slot base address.
 * offset: register offset in bytes (must be 4-byte aligned).
 */
static uint32_t mmio_read32(uint64_t base, uint32_t offset)
{
    volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(base + offset);
    return *reg;
}

/*
 * virtio_mmio_blk_probe - Scan platform MMIO bus for a VirtIO block device.
 */
uint64_t virtio_mmio_blk_probe(void)
{
    uint32_t slot;

    for (slot = 0u; slot < VIRTIO_MMIO_SLOT_COUNT; slot++) {
        uint64_t base;
        uint32_t magic;
        uint32_t dev_id;

        base   = VIRTIO_MMIO_BASE_ADDR + (uint64_t)slot * VIRTIO_MMIO_SLOT_STRIDE;
        magic  = mmio_read32(base, VIRTIO_MMIO_REG_MAGIC);
        if (magic != VIRTIO_MMIO_MAGIC_VALUE) {
            continue;
        }
        dev_id = mmio_read32(base, VIRTIO_MMIO_REG_DEVICE_ID);
        if (dev_id == VIRTIO_DEVICE_ID_BLOCK) {
            return base;
        }
    }

    return 0UL;
}

