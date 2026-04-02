/*
 * Zapada - src/managed/Zapada.Fs.Fat32/KernelCalls.cs
 *
 * Phase 3.3 — managed bridge for Zapada.Kernel InternalCalls.
 *
 * Exposes the kernel-side boot-partition LBA store so that Fat32Reader
 * can retrieve the FAT32 partition start LBA during Initialize() without
 * having to re-parse the GPT partition table.
 *
 * The LBA was stored by Zapada.Boot.BootLoader via Zapada.Kernel.SetBootPartLba()
 * after a successful Fat32.Mount() call.
 *
 * InternalCall table entry in src/kernel/clr/internalcall.c:
 *   { "Zapada.Kernel", "GetBootPartLba", 0, CLR_IC_KIND_I32,
 *     { CLR_IC_KIND_NONE }, ic_get_boot_part_lba }
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
         * GetBootPartLba() -> int
         *
         * Returns the boot partition start LBA previously stored by
         * Zapada.Boot.BootLoader via SetBootPartLba().
         *
         * Returns 0 if SetBootPartLba() was never called (kernel default).
         */
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern int GetBootPartLba();
    }
}


