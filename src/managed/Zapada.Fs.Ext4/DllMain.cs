using System;
using Zapada.Storage;

namespace Zapada.Fs.Ext4;

public static class DllMain
{
    public static int Initialize()
    {
        Console.Write("[Boot] Ext4 driver initialized\n");

        int reg = DriverRegistry.Register(new Ext4Probe());
        if (reg == StorageStatus.Ok)
        {
            Console.Write("[Storage] DriverRegistry: probe registered ext4\n");
        }
        else if (reg == StorageStatus.AlreadyExists)
        {
            Console.Write("[Storage] DriverRegistry: probe already registered ext4\n");
        }
        else
        {
            Console.Write("[Storage] DriverRegistry: probe register failed ext4\n");
        }

        Console.Write("[Gate] Phase-Ext4Driver\n");
        return 1;
    }
}
