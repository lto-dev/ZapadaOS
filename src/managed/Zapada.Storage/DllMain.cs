using System;

namespace Zapada.Storage;

public static class DllMain
{
    public static int Initialize()
    {
        Console.Write("[Storage] Zapada.Storage initialized\n");

        DriverRegistry.Initialize();
        Console.Write("[Storage] DriverRegistry initialized\n");

        // Create RamFsVolume and initialize
        RamFsVolume ramfs = new RamFsVolume();
        ramfs.InitializeRamFs();

        // Use through MountedVolume base class reference (tests callvirt on abstract override)
        MountedVolume vol = ramfs;

        Console.Write("[Storage] RamFs: initialized with ");
        Console.Write(Ramdisk.FileCount());
        Console.Write(" files\n");

        // Test Resolve through virtual dispatch
        int node = vol.Resolve("Zapada.Test.Hello.dll");
        Console.Write("[Storage] RamFs: resolve -> node ");
        Console.Write(node);
        Console.Write("\n");

        if (node <= 0) {
            Console.Write("[Storage] RamFs: resolve FAILED\n");
            Console.Write("[Gate] Phase-Storage\n");
            return 1;
        }

        // Test Open through virtual dispatch
        int token = vol.Open(node, FileAccessIntent.ReadOnly);
        Console.Write("[Storage] RamFs: open -> token ");
        Console.Write(token);
        Console.Write("\n");

        if (token < 0) {
            Console.Write("[Storage] RamFs: open FAILED\n");
            Console.Write("[Gate] Phase-Storage\n");
            return 1;
        }

        // Test Read through virtual dispatch
        byte[] buf = new byte[2];
        int bytesRead = vol.Read(token, buf, 0, 2);
        Console.Write("[Storage] RamFs: read -> ");
        Console.Write(bytesRead);
        Console.Write(" bytes\n");

        if (bytesRead >= 2 && buf[0] == 77 && buf[1] == 90) {  // 'M'=77, 'Z'=90
            Console.Write("[Storage] RamFs: MZ header OK\n");
        } else {
            Console.Write("[Storage] RamFs: MZ check FAILED\n");
        }

        // Test Close through virtual dispatch
        int closeResult = vol.Close(token);

        // Test GetDriverKey through virtual dispatch
        string driverKey = vol.GetDriverKey();
        Console.Write("[Storage] RamFs: driver=");
        Console.Write(driverKey);
        Console.Write("\n");

        bool ramFsPassed = (bytesRead >= 2 && buf[0] == 77 && buf[1] == 90);
        if (ramFsPassed) {
            Console.Write("[Gate] Phase-RamFs\n");
        }

        Console.Write("[Gate] Phase-Storage\n");
        return 1;
    }
}
