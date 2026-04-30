namespace Zapada.Storage;

public sealed class DevFsVolume : MountedVolume
{
    public override string GetDriverKey() { return "devfs"; }
    public override string GetDisplayName() { return "Zapada device namespace"; }
    public override string GetVolumeLabel() { return "devfs"; }
    public override string GetVolumeId() { return "dev"; }

    public override int GetRoot()
    {
        return 0;
    }

    public override int Resolve(string path)
    {
        if (path == null)
            return StorageStatus.InvalidArgument;

        if (path.Length == 0 || path == "/")
            return 0;

        string fullPath = path;
        if (path[0] == '/')
            fullPath = string.Concat("/dev", path);
        else
            fullPath = string.Concat("/dev/", path);

        int index = DeviceRegistry.FindNodeIndex(fullPath);
        if (index < 0)
            return StorageStatus.NotFound;

        return index + 1;
    }

    public override int ListDirectory(int nodeHandle, DirectoryEntrySink sink)
    {
        if (sink == null)
            return StorageStatus.InvalidArgument;
        if (nodeHandle != 0)
            return StorageStatus.NotDirectory;

        int count = DeviceRegistry.Count();
        for (int i = 0; i < count; i++)
        {
            DeviceNode? node = DeviceRegistry.Get(i);
            if (node == null)
                continue;

            sink.OnEntry(i + 1, node.Name, MapNodeKind(node.Kind));
        }

        return StorageStatus.Ok;
    }

    public override int Stat(int nodeHandle, NodeFacts facts)
    {
        if (facts == null)
            return StorageStatus.InvalidArgument;

        if (nodeHandle == 0)
        {
            facts.NodeHandle = 0;
            facts.NodeKind = 2;
            facts.Size = 0;
            facts.ModifiedTime = 0;
            facts.Permissions = 0;
            return StorageStatus.Ok;
        }

        DeviceNode? node = DeviceRegistry.Get(nodeHandle - 1);
        if (node == null)
            return StorageStatus.NotFound;

        facts.NodeHandle = nodeHandle;
        facts.NodeKind = MapNodeKind(node.Kind);
        facts.Size = 0;
        facts.ModifiedTime = 0;
        facts.Permissions = node.Permissions;
        return StorageStatus.Ok;
    }

    public override int Open(int nodeHandle, int accessIntent)
    {
        if (nodeHandle <= 0)
            return StorageStatus.NotFile;

        DeviceNode? node = DeviceRegistry.Get(nodeHandle - 1);
        if (node == null)
            return StorageStatus.NotFound;

        if (accessIntent == FileAccessIntent.ReadOnly)
        {
            if (node.Kind == DeviceKind.Null || node.Kind == DeviceKind.Zero || node.Kind == DeviceKind.Random)
                return nodeHandle;

            return StorageStatus.NotSupported;
        }

        if (accessIntent == FileAccessIntent.ReadWrite && node.Kind == DeviceKind.Null)
            return nodeHandle;

        return StorageStatus.PermissionDenied;
    }

    public override int Read(int fileToken, byte[] buffer, int offset, int count)
    {
        if (buffer == null || offset < 0 || count < 0 || offset + count > buffer.Length)
            return StorageStatus.InvalidArgument;

        if (fileToken <= 0)
            return StorageStatus.InvalidArgument;

        DeviceNode? node = DeviceRegistry.Get(fileToken - 1);
        if (node == null)
            return StorageStatus.NotFound;

        if (node.Kind == DeviceKind.Null)
            return 0;

        if (node.Kind == DeviceKind.Zero)
        {
            for (int i = 0; i < count; i++)
                buffer[offset + i] = 0;

            return count;
        }

        if (node.Kind == DeviceKind.Random)
            return EntropyService.FillFromDevice(node.TargetHandle, buffer, offset, count);

        return StorageStatus.NotSupported;
    }

    public override int Seek(int fileToken, long absoluteOffset)
    {
        return StorageStatus.NotSupported;
    }

    public override int Close(int fileToken)
    {
        if (fileToken <= 0)
            return StorageStatus.InvalidArgument;

        return StorageStatus.Ok;
    }

    private static int MapNodeKind(int kind)
    {
        if (kind == DeviceKind.Directory)
            return 2;

        return 0;
    }
}
