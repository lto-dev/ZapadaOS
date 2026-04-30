using System;
using Zapada.Drivers.Hal;
using Zapada.Storage;

namespace Zapada.Drivers;

internal static class VirtioBlockManager
{
    private const int MaxManagedDisks = 4;

    public static int RegisterManagedDevices()
    {
        int count = PciBus.DeviceCount();
        int registered = 0;

        for (int i = 0; i < count && registered < MaxManagedDisks; i++)
        {
            PciDeviceInfo device = PciBus.GetDevice(i);
            if (device == null || device.VendorId != VirtioConstants.VendorId || device.DeviceId != VirtioConstants.BlockModernDeviceId)
                continue;

            string name = DeviceName(registered);
            VirtioPciBlockDevice block = new VirtioPciBlockDevice();
            int initRc = block.Initialize(name, device);
            if (initRc != StorageStatus.Ok)
            {
                Console.Write("[VirtioBlock] managed init failed for ");
                Console.Write(name);
                Console.Write(" rc=");
                Console.Write(initRc);
                Console.Write("\n");
                continue;
            }

            int registerRc = BlockDeviceRegistry.Register(block);
            if (registerRc == StorageStatus.Ok || registerRc == StorageStatus.AlreadyExists)
            {
                DriverRegistry.AddUse("virtio-blk");
                DriverRegistry.RegisterService(string.Concat("block.device:", name), DriverServiceKind.BlockDevice, "virtio-blk", name);
                Console.Write("[Storage] block registered: ");
                Console.Write(name);
                Console.Write(" managed-virtio\n");
                registered++;
            }
            else
            {
                Console.Write("[VirtioBlock] block register failed rc=");
                Console.Write(registerRc);
                Console.Write("\n");
            }
        }

        if (registered > 0)
            Console.Write("[Gate] Phase-ManagedVirtioBlock\n");

        return registered;
    }

    private static string DeviceName(int index)
    {
        if (index == 0) return "vda";
        if (index == 1) return "vdb";
        if (index == 2) return "vdc";
        if (index == 3) return "vdd";
        return "vdx";
    }
}
