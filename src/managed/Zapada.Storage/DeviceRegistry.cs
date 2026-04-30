using System;

namespace Zapada.Storage;

public static class DeviceRegistry
{
    private const int MaxDeviceNodes = 64;
    private static DeviceNode?[]? s_nodes;
    private static int s_count;

    public static void Initialize()
    {
        s_nodes = new DeviceNode[MaxDeviceNodes];
        s_count = 0;
    }

    public static int RegisterNode(DeviceNode node)
    {
        if (node == null || node.Path == null || node.Path.Length == 0 || node.Name == null || node.Name.Length == 0)
            return StorageStatus.InvalidArgument;

        EnsureInitialized();

        if (FindNode(node.Path) != null)
            return StorageStatus.AlreadyExists;

        if (s_nodes == null || s_count >= MaxDeviceNodes)
            return StorageStatus.TableFull;

        s_nodes[s_count] = node;
        s_count++;
        return StorageStatus.Ok;
    }

    public static int RegisterNode(string path, string name, int kind, string serviceKey, string driverKey, int permissions, int targetHandle)
    {
        DeviceNode node = new DeviceNode();
        node.Initialize(path, name, kind, serviceKey, driverKey, permissions, targetHandle);
        return RegisterNode(node);
    }

    public static int Count()
    {
        EnsureInitialized();
        return s_count;
    }

    public static DeviceNode? Get(int index)
    {
        EnsureInitialized();

        if (s_nodes == null || index < 0 || index >= s_count)
            return null;

        return s_nodes[index];
    }

    public static DeviceNode? FindNode(string path)
    {
        EnsureInitialized();

        if (s_nodes == null || path == null)
            return null;

        for (int i = 0; i < s_count; i++)
        {
            DeviceNode? node = s_nodes[i];
            if (node != null && node.Path == path)
                return node;
        }

        return null;
    }

    public static int FindNodeIndex(string path)
    {
        EnsureInitialized();

        if (s_nodes == null || path == null)
            return -1;

        for (int i = 0; i < s_count; i++)
        {
            DeviceNode? node = s_nodes[i];
            if (node != null && node.Path == path)
                return i;
        }

        return -1;
    }

    public static int PrintNodes()
    {
        EnsureInitialized();

        Console.Write("device                 kind service driver\n");
        if (s_nodes == null)
            return StorageStatus.Ok;

        for (int i = 0; i < s_count; i++)
        {
            DeviceNode? node = s_nodes[i];
            if (node == null)
                continue;

            Console.Write(node.Path);
            Console.Write(" ");
            Console.Write(KindToString(node.Kind));
            Console.Write(" ");
            Console.Write(node.ServiceKey);
            Console.Write(" ");
            Console.Write(node.DriverKey);
            Console.Write("\n");
        }

        return StorageStatus.Ok;
    }

    public static string KindToString(int kind)
    {
        if (kind == DeviceKind.Directory) return "directory";
        if (kind == DeviceKind.Block) return "block";
        if (kind == DeviceKind.Character) return "char";
        if (kind == DeviceKind.Console) return "console";
        if (kind == DeviceKind.Null) return "null";
        if (kind == DeviceKind.Zero) return "zero";
        if (kind == DeviceKind.Random) return "random";
        return "unknown";
    }

    private static void EnsureInitialized()
    {
        if (s_nodes == null)
            Initialize();
    }
}
