/*
 * Zapada - src/managed/Zapada.Boot/KernelCalls.cs
 *
 * Phase 3.3 — managed bridge for Zapada.Kernel InternalCalls.
 *
 * Exposes kernel-side boot state that still belongs at the native boundary.
 * Storage no longer uses a boot-partition LBA handoff: managed block devices
 * register through Zapada.Storage.BlockDeviceRegistry and partitions are
 * discovered through Zapada.Storage.PartitionScanner.
 */

using System.Runtime.CompilerServices;

namespace Zapada
{
    /*
     * Kernel - thin bridge to kernel-side state.
     *
     * All methods are InternalCalls dispatched through the ZACLR native-provider
     * registry.
     */
    internal static class Kernel
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern string GetBootCommandLine();
    }
}


