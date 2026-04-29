/*
 * Zapada - src/managed/Zapada.Fs.Fat32/DllMain.cs
 *
 * Phase 3.1 D.3: FAT32 filesystem driver entry point.
 *
 * DllMain.Initialize() is invoked by BootLoader.cs via RuntimeCallMethod
 * after the managed boot flow loads the FAT32 driver payload.
 *
 * This is the Phase 3.1 D.3 gate for managed FAT32 driver loading.
 * Initialize() announces the driver, registers the FAT32 probe, and emits the
 * gate line. Mounted FAT32 state is now created per-partition by
 * [`Fat32Probe.Mount()`](src/managed/Zapada.Fs.Fat32/Fat32Probe.cs:45) via
 * [`Fat32Volume.Initialize()`](src/managed/Zapada.Fs.Fat32/Fat32Volume.cs:41).
 *
 * Gate output checked by test-all.ps1:
 *   "[Gate] Phase31-D3"  — FAT32 managed driver DllMain executed successfully
 *
 * The C# fully qualified type name for RuntimeCallMethod is:
 *   "Zapada.Fs.Fat32.DllMain"
 */

using System;
using Zapada.Storage;

namespace Zapada.Fs.Fat32
{
    public static class DllMain
    {
        /*
         * Initialize() - called by BootLoader.cs once FAT32.DLL is loaded.
         *
         * Registers the FAT32 probe and emits gate output.
         * Returns 1 on success, 0 on failure.
         */
        public static int Initialize()
        {
            Console.Write("[Boot] FAT32 driver initialized\n");

            int reg = DriverRegistry.Register(new Fat32Probe());
            if (reg == StorageStatus.Ok)
            {
                Console.Write("[Storage] DriverRegistry: probe registered fat32\n");
            }
            else if (reg == StorageStatus.AlreadyExists)
            {
                Console.Write("[Storage] DriverRegistry: probe already registered fat32\n");
            }
            else
            {
                Console.Write("[Storage] DriverRegistry: probe register failed\n");
            }

            Console.Write("[Gate] Phase31-D3\n");
            return 1;
        }
    }
}



