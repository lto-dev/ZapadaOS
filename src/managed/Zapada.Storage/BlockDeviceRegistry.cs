using System;

namespace Zapada.Storage;

public static class BlockDeviceRegistry
{
    private const int MaxBlockDevices = 16;
    private static BlockDevice?[]? s_devices;
    private static int s_count;

    public static void Initialize()
    {
        s_devices = new BlockDevice[MaxBlockDevices];
        s_count = 0;
    }

    public static int Register(BlockDevice device)
    {
        if (device == null)
            return StorageStatus.InvalidArgument;

        EnsureInitialized();

        BlockDeviceInfo info = device.GetInfo();
        if (info == null || info.Name == null || info.Name.Length == 0)
            return StorageStatus.InvalidArgument;

        if (FindByName(info.Name) != null)
            return StorageStatus.AlreadyExists;

        if (s_count >= MaxBlockDevices || s_devices == null)
            return StorageStatus.TableFull;

        s_devices[s_count] = device;
        s_count++;
        return StorageStatus.Ok;
    }

    public static int Count()
    {
        EnsureInitialized();

        return s_count;
    }

    public static int PrintDevices()
    {
        EnsureInitialized();

        Console.Write("block                  driver sector-size sectors flags\n");
        if (s_devices == null)
            return StorageStatus.Ok;

        for (int i = 0; i < s_count; i++)
        {
            BlockDevice? device = s_devices[i];
            if (device == null)
                continue;

            BlockDeviceInfo info = device.GetInfo();
            if (info == null)
                continue;

            Console.Write(info.Name);
            Console.Write(" ");
            Console.Write(info.DriverKey);
            Console.Write(" ");
            Console.Write(info.SectorSize);
            Console.Write(" ");
            Console.Write(info.SectorCount);
            Console.Write(" ");
            Console.Write(info.ReadOnly ? "ro" : "rw");
            Console.Write(" ");
            Console.Write(info.Removable ? "removable" : "fixed");
            Console.Write("\n");
        }

        return StorageStatus.Ok;
    }

    public static BlockDevice? Get(int index)
    {
        EnsureInitialized();

        if (s_devices == null || index < 0 || index >= s_count)
            return null;

        return s_devices[index];
    }

    public static BlockDevice? FindByName(string name)
    {
        EnsureInitialized();

        if (s_devices == null || name == null)
            return null;

        for (int i = 0; i < s_count; i++)
        {
            BlockDevice? device = s_devices[i];
            if (device == null)
                continue;

            BlockDeviceInfo info = device.GetInfo();
            if (info != null && info.Name == name)
                return device;
        }

        return null;
    }

    private static void EnsureInitialized()
    {
        if (s_devices == null)
            Initialize();
    }
}
