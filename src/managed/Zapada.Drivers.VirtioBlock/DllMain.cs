/*
 * Zapada - src/managed/Zapada.Drivers.VirtioBlock/DllMain.cs
 *
 * Phase 3.1: VirtioBlock driver entry point.
 *
 * DllMain.Initialize() is invoked by BootLoader.cs via RuntimeCallMethod
 * after the managed boot flow loads the driver payload.
 *
 * This is the Phase 3.1 gate for managed driver loading.  Initialize()
 * prints the driver gate line and returns 1 to signal success.
 *
 * Gate output checked by test-all.ps1:
 *   "[Gate] Phase31-D1"  — VirtioBlock driver DllMain executed successfully
 *
 * The active VirtIO block I/O path is managed.  Initialize() registers the
 * driver identity, discovers modern VirtIO PCI devices through DriverHal, and
 * registers vda/vdb BlockDevice instances backed by managed MMIO/DMA queues.
 */

using System;
using Zapada.Storage;

namespace Zapada.Drivers
{
    public static class DllMain
    {
        /*
         * Initialize() - called by BootLoader.cs once VBLK.DLL is loaded.
         *
         * Returns 1 on success, 0 on failure.
         * RuntimeCallMethod() in ic_runtime_call_method() does not inspect the
         * return value; it returns 1 to BootLoader if the managed method executed
         * without interpreter error.  The int return here is for future use.
         */
        public static int Initialize()
        {
            DriverRegistry.Register(
                "virtio-blk",
                "Zapada.Drivers.VirtioBlock",
                "block.device:vda,block.device:vdb,hal.smoke",
                "Zapada.Drivers.Hal,Zapada.Storage",
                "pci:1af4:1001,pci:1af4:1042",
                DriverState.Started);

            Console.Write("[Boot] VirtioBlock managed bridge initialized\n");
            Console.Write("[Gate] Phase31-D1\n");

            int managedDevices = VirtioBlockManager.RegisterManagedDevices();
            if (managedDevices <= 0)
            {
                Console.Write("[Boot] VirtioBlock managed device registration unavailable\n");
            }

            if (VirtioBlockProbe.RunSmoke() == 0)
            {
                Console.Write("[Boot] VirtioBlock managed HAL smoke failed\n");
                return 0;
            }

            return 1;
        }
    }
}



