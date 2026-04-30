/*
 * Zapada - src/kernel/arch/aarch64/pci.c
 *
 * AArch64 PCIe ECAM configuration space access.
 *
 * This is the AArch64 equivalent of the x86_64 PCI config scanner, except the
 * transport is MMIO through an ECAM window rather than port I/O.
 */

#include <kernel/arch/aarch64/pci.h>
#include <kernel/types.h>

static uint64_t aa64_pci_cfg_addr(uint64_t ecam_base, uint8_t bus, uint8_t dev,
                                  uint8_t fn, uint16_t reg)
{
    return ecam_base
         + ((uint64_t)bus << 20)
         + ((uint64_t)(dev & 0x1Fu) << 15)
         + ((uint64_t)(fn & 0x07u) << 12)
         + (uint64_t)(reg & 0x0FFFu);
}

uint32_t aa64_pci_cfg_read32(uint64_t ecam_base, uint8_t bus, uint8_t dev,
                             uint8_t fn, uint16_t reg)
{
    volatile uint32_t *cfg;

    cfg = (volatile uint32_t *)(uintptr_t)aa64_pci_cfg_addr(ecam_base, bus, dev,
                                                            fn, (uint16_t)(reg & 0xFFCu));
    return *cfg;
}

uint16_t aa64_pci_cfg_read16(uint64_t ecam_base, uint8_t bus, uint8_t dev,
                             uint8_t fn, uint16_t reg)
{
    uint32_t dword;

    dword = aa64_pci_cfg_read32(ecam_base, bus, dev, fn, (uint16_t)(reg & 0xFFCu));
    if ((reg & 2u) != 0u) {
        return (uint16_t)(dword >> 16);
    }
    return (uint16_t)(dword & 0xFFFFu);
}

