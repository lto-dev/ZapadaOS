using Zapada.Storage;

namespace Zapada.Fs.Vfs;

public sealed class ProcFsVolume : MountedVolume
{
    private const int NodeRoot = 0;
    private const int NodeMounts = 1;
    private const int NodeDrivers = 2;
    private const int NodeDevices = 3;
    private const int NodePartitions = 4;
    private const int NodeInterrupts = 5;
    private const int NodeMemInfo = 6;
    private const int NodeUptime = 7;

    private const int MaxTokens = 8;

    private readonly ProcFsProvider _provider;
    private bool[] _tokenOpen = null!;
    private string[] _tokenContent = null!;
    private int[] _tokenOffset = null!;

    public ProcFsVolume(ProcFsProvider provider)
    {
        _provider = provider;
        _tokenOpen = new bool[MaxTokens];
        _tokenContent = new string[MaxTokens];
        _tokenOffset = new int[MaxTokens];
    }

    public override string GetDriverKey() { return "procfs"; }
    public override string GetDisplayName() { return "Zapada process namespace"; }
    public override string GetVolumeLabel() { return "procfs"; }
    public override string GetVolumeId() { return "proc"; }

    public override int GetRoot()
    {
        return NodeRoot;
    }

    public override int Resolve(string path)
    {
        if (path == null)
            return StorageStatus.InvalidArgument;

        if (path.Length == 0 || path == "/")
            return NodeRoot;

        if (PathEquals(path, "/mounts") || PathEquals(path, "mounts")) return NodeMounts;
        if (PathEquals(path, "/drivers") || PathEquals(path, "drivers")) return NodeDrivers;
        if (PathEquals(path, "/devices") || PathEquals(path, "devices")) return NodeDevices;
        if (PathEquals(path, "/partitions") || PathEquals(path, "partitions")) return NodePartitions;
        if (PathEquals(path, "/interrupts") || PathEquals(path, "interrupts")) return NodeInterrupts;
        if (PathEquals(path, "/meminfo") || PathEquals(path, "meminfo")) return NodeMemInfo;
        if (PathEquals(path, "/uptime") || PathEquals(path, "uptime")) return NodeUptime;

        return StorageStatus.NotFound;
    }

    public override int ListDirectory(int nodeHandle, DirectoryEntrySink sink)
    {
        if (sink == null)
            return StorageStatus.InvalidArgument;
        if (nodeHandle != NodeRoot)
            return StorageStatus.NotDirectory;

        sink.OnEntry(NodeMounts, "mounts", 1);
        sink.OnEntry(NodeDrivers, "drivers", 1);
        sink.OnEntry(NodeDevices, "devices", 1);
        sink.OnEntry(NodePartitions, "partitions", 1);
        sink.OnEntry(NodeInterrupts, "interrupts", 1);
        sink.OnEntry(NodeMemInfo, "meminfo", 1);
        sink.OnEntry(NodeUptime, "uptime", 1);
        return StorageStatus.Ok;
    }

    public override int Stat(int nodeHandle, NodeFacts facts)
    {
        if (facts == null)
            return StorageStatus.InvalidArgument;

        facts.NodeHandle = nodeHandle;
        facts.ModifiedTime = 0;
        facts.Permissions = FileAccessIntent.ReadOnly;

        if (nodeHandle == NodeRoot)
        {
            facts.NodeKind = 2;
            facts.Size = 0;
            return StorageStatus.Ok;
        }

        if (!IsFileNode(nodeHandle))
            return StorageStatus.NotFound;

        string content = BuildContent(nodeHandle);
        facts.NodeKind = 1;
        facts.Size = content.Length;
        return StorageStatus.Ok;
    }

    public override int Open(int nodeHandle, int accessIntent)
    {
        if (accessIntent != FileAccessIntent.ReadOnly)
            return StorageStatus.NotSupported;
        if (!IsFileNode(nodeHandle))
            return StorageStatus.NotFile;
        if (_provider == null)
            return StorageStatus.NotMounted;

        string content = BuildContent(nodeHandle);
        for (int i = 0; i < MaxTokens; i++)
        {
            if (!_tokenOpen[i])
            {
                _tokenOpen[i] = true;
                _tokenContent[i] = content;
                _tokenOffset[i] = 0;
                return i;
            }
        }

        return StorageStatus.TableFull;
    }

    public override int Read(int fileToken, byte[] buffer, int offset, int count)
    {
        if (fileToken < 0 || fileToken >= MaxTokens || !_tokenOpen[fileToken])
            return StorageStatus.InvalidArgument;
        if (buffer == null || offset < 0 || count <= 0)
            return StorageStatus.InvalidArgument;
        if (offset + count > buffer.Length)
            return StorageStatus.InvalidArgument;

        string content = _tokenContent[fileToken];
        if (content == null)
            content = "";

        int sourceOffset = _tokenOffset[fileToken];
        if (sourceOffset >= content.Length)
            return 0;

        int available = content.Length - sourceOffset;
        int toCopy = count;
        if (toCopy > available)
            toCopy = available;

        for (int i = 0; i < toCopy; i++)
        {
            char ch = content[sourceOffset + i];
            buffer[offset + i] = ch <= 127 ? (byte)ch : (byte)'?';
        }

        _tokenOffset[fileToken] = sourceOffset + toCopy;
        return toCopy;
    }

    public override int Seek(int fileToken, long absoluteOffset)
    {
        if (fileToken < 0 || fileToken >= MaxTokens || !_tokenOpen[fileToken])
            return StorageStatus.InvalidArgument;
        if (absoluteOffset < 0 || absoluteOffset > 2147483647L)
            return StorageStatus.InvalidArgument;

        _tokenOffset[fileToken] = (int)absoluteOffset;
        return StorageStatus.Ok;
    }

    public override int Close(int fileToken)
    {
        if (fileToken < 0 || fileToken >= MaxTokens)
            return StorageStatus.InvalidArgument;

        _tokenOpen[fileToken] = false;
        _tokenContent[fileToken] = "";
        _tokenOffset[fileToken] = 0;
        return StorageStatus.Ok;
    }

    private string BuildContent(int nodeHandle)
    {
        if (_provider == null)
            return "procfs provider unavailable\n";

        if (nodeHandle == NodeMounts) return _provider.BuildMounts();
        if (nodeHandle == NodeDrivers) return _provider.BuildDrivers();
        if (nodeHandle == NodeDevices) return _provider.BuildDevices();
        if (nodeHandle == NodePartitions) return _provider.BuildPartitions();
        if (nodeHandle == NodeInterrupts) return _provider.BuildInterrupts();
        if (nodeHandle == NodeMemInfo) return _provider.BuildMemInfo();
        if (nodeHandle == NodeUptime) return _provider.BuildUptime();

        return "";
    }

    private static bool IsFileNode(int nodeHandle)
    {
        return nodeHandle >= NodeMounts && nodeHandle <= NodeUptime;
    }

    private static bool PathEquals(string path, string expected)
    {
        if (path == null || expected == null || path.Length != expected.Length)
            return false;

        for (int i = 0; i < expected.Length; i++)
        {
            if (path[i] != expected[i])
                return false;
        }

        return true;
    }
}
