using System;

namespace Zapada.Storage;

public static class PartitionRegistry
{
    private const int MaxPartitions = 64;
    private static PartitionInfo?[]? s_partitions;
    private static int s_count;

    public static void Initialize()
    {
        s_partitions = new PartitionInfo[MaxPartitions];
        s_count = 0;
    }

    public static int Register(PartitionInfo partition)
    {
        if (partition == null || partition.Name == null || partition.Name.Length == 0)
            return StorageStatus.InvalidArgument;

        EnsureInitialized();

        if (FindByName(partition.Name) != null)
            return StorageStatus.AlreadyExists;

        if (s_count >= MaxPartitions || s_partitions == null)
            return StorageStatus.TableFull;

        s_partitions[s_count] = partition;
        s_count++;
        return StorageStatus.Ok;
    }

    public static int Count()
    {
        EnsureInitialized();
        return s_count;
    }

    public static PartitionInfo? Get(int index)
    {
        EnsureInitialized();

        if (s_partitions == null || index < 0 || index >= s_count)
            return null;

        return s_partitions[index];
    }

    public static PartitionInfo? FindByName(string name)
    {
        EnsureInitialized();

        if (s_partitions == null || name == null)
            return null;

        for (int i = 0; i < s_count; i++)
        {
            PartitionInfo? partition = s_partitions[i];
            if (partition != null && partition.Name == name)
                return partition;
        }

        return null;
    }

    public static PartitionInfo? FindByLabel(string label)
    {
        EnsureInitialized();

        if (s_partitions == null || label == null)
            return null;

        for (int i = 0; i < s_count; i++)
        {
            PartitionInfo? partition = s_partitions[i];
            if (partition != null && partition.Label == label)
                return partition;
        }

        return null;
    }

    public static BlockDevicePartitionView? CreateView(PartitionInfo partition)
    {
        if (partition == null || partition.DeviceName == null || partition.DeviceName.Length == 0)
            return null;

        BlockDevice? device = BlockDeviceRegistry.FindByName(partition.DeviceName);
        if (device == null)
            return null;

        BlockDevicePartitionView view = new BlockDevicePartitionView();
        view.Initialize(device, partition.Name, partition.StartLba, partition.SectorCount, partition.SchemeKind);
        return view;
    }

    public static int PrintPartitions()
    {
        EnsureInitialized();

        Console.Write("partition              device label start sectors scheme\n");
        if (s_partitions == null)
            return StorageStatus.Ok;

        for (int i = 0; i < s_count; i++)
        {
            PartitionInfo? partition = s_partitions[i];
            if (partition == null)
                continue;

            Console.Write(partition.Name);
            Console.Write(" ");
            Console.Write(partition.DeviceName);
            Console.Write(" ");
            Console.Write(partition.Label);
            Console.Write(" ");
            Console.Write(partition.StartLba);
            Console.Write(" ");
            Console.Write(partition.SectorCount);
            Console.Write(" ");
            Console.Write(partition.SchemeKind == 2 ? "gpt" : "unknown");
            Console.Write("\n");
        }

        return StorageStatus.Ok;
    }

    private static void EnsureInitialized()
    {
        if (s_partitions == null)
            Initialize();
    }
}
