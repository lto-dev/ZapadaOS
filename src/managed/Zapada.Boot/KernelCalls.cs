/*
 * Zapada - src/managed/Zapada.Boot/KernelCalls.cs
 *
 * Phase 3.3 — managed bridge for Zapada.Kernel InternalCalls.
 *
 * Exposes the kernel-side boot-partition LBA store so that BootLoader
 * can record the FAT32 partition start LBA after a successful mount.
 * The value is later retrieved by the Zapada.Fs.Fat32 driver DLL during
 * its own Initialize() call without needing to re-parse GPT metadata.
 *
 * InternalCall table entry in src/kernel/clr/internalcall.c:
 *   { "Zapada.Kernel", "SetBootPartLba", 1, CLR_IC_KIND_NONE,
 *     { CLR_IC_KIND_I32 }, ic_set_boot_part_lba }
 */

using System.Runtime.CompilerServices;

namespace Zapada
{
    /*
     * Kernel - thin bridge to kernel-side state.
     *
     * All methods are InternalCalls dispatched by the CIL interpreter to
     * native C functions in src/kernel/clr/internalcall.c.
     */
    internal static class Kernel
    {
        /*
         * SetBootPartLba(int lba)
         *
         * Stores the boot partition start LBA in a kernel-side static variable.
         * Must be called after Fat32.Mount() succeeds so that the FAT32 driver
         * DLL can retrieve it via GetBootPartLba().
         *
         * Returns void.
         */
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void SetBootPartLba(int lba);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern string GetBootCommandLine();
    }
}


