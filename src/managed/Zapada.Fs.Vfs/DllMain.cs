using System;

namespace Zapada.Fs.Vfs;

/// <summary>
/// Entry point for VFS.DLL.
/// Assumes BootLoader already initialized VFS with a bootstrap root mount.
/// Verifies open/read/close over the mounted-volume contract.
/// </summary>
public static class DllMain
{
    public static int Initialize()
    {
        if (!Vfs.IsInitialized)
        {
            Console.Write("[VFS] NamespaceGateway: not initialized\n");
            return 0;
        }

        Console.Write("[VFS] NamespaceGateway: initialized\n");
        Console.Write("[Boot] VFS initialized\n");
        Console.Write("[VFS] MountCount=");
        Console.Write(Vfs.MountCount);
        Console.Write("\n");
        Console.Write("[Gate] Phase31-D4\n");

        Console.Write("[VFS] Open: /Zapada.Test.Hello.dll\n");
        int fd = Vfs.Open("/Zapada.Test.Hello.dll");
        if (fd < 0)
        {
            Console.Write("[VFS] Open failed\n");
            return 0;
        }
 
        byte[] hdr = new byte[2];
        int nr = Vfs.Read(fd, hdr, 0, 2);
        Vfs.Close(fd);
        Console.Write("[VFS] Read: fd done\n");
        Console.Write("[VFS] Close: fd done\n");
 
        if (nr == 2 && hdr[0] == 0x4D && hdr[1] == 0x5A)
        {
            Console.Write("[VFS] Read OK: MZ\n");
            Console.Write("[Gate] Phase-VfsRedesign\n");
        }
        else
        {
            Console.Write("[VFS] Read mismatch\n");
        }

        return 0;
    }
}

