using System;

namespace Zapada.Storage;

public sealed class RamFsVolume : MountedVolume
{
    private int _fileCount;
    private long[] _readOffsets = null!;
    private bool[] _tokenOpen = null!;
    private int[] _tokenFileIndex = null!;  // maps token -> file index
    private const int MaxTokens = 16;

    public void InitializeRamFs()
    {
        _fileCount = Ramdisk.FileCount();
        _readOffsets = new long[MaxTokens];
        _tokenOpen = new bool[MaxTokens];
        _tokenFileIndex = new int[MaxTokens];
    }

    public override string GetDriverKey() { return "ramfs"; }
    public override string GetDisplayName() { return "initramfs bootstrap root"; }
    public override string GetVolumeLabel() { return "initramfs"; }
    public override string GetVolumeId() { return "boot"; }

    public override int Resolve(string path)
    {
        // Strip leading slash for ramdisk lookup
        string lookupName = path;
        int pathLength = path.Length;

        bool hasLeadingSlash = false;
        if (pathLength > 1)
        {
            hasLeadingSlash = path[0] == '/';
        }

        if (hasLeadingSlash)
        {
            lookupName = path.Substring(1);
        }

        int idx = Ramdisk.Lookup(lookupName);
        if (idx < 0)
            return StorageStatus.NotFound;

        // Node handle = file index + 1 (0 reserved for root)
        return idx + 1;
    }

    public override int GetRoot()
    {
        return 0;
    }

    public override int ListDirectory(int nodeHandle, DirectoryEntrySink sink)
    {
        if (nodeHandle != 0)
            return StorageStatus.NotDirectory;

        int count = Ramdisk.FileCount();
        for (int i = 0; i < count; i++)
        {
            string name = Ramdisk.GetFileName(i);
            if (name != null)
            {
                sink.OnEntry(i + 1, name, 1); // 1 = file
            }
        }
        return StorageStatus.Ok;
    }

    public override int Stat(int nodeHandle, NodeFacts facts)
    {
        if (nodeHandle == 0)
        {
            facts.NodeHandle = 0;
            facts.NodeKind = 2; // directory
            facts.Size = 0;
            return StorageStatus.Ok;
        }

        int fileIndex = nodeHandle - 1;
        if (fileIndex < 0 || fileIndex >= _fileCount)
            return StorageStatus.NotFound;

        facts.NodeHandle = nodeHandle;
        facts.NodeKind = 1; // file
        facts.Size = Ramdisk.GetFileSize(fileIndex);
        return StorageStatus.Ok;
    }

    public override int Open(int nodeHandle, int accessIntent)
    {
        if (accessIntent != FileAccessIntent.ReadOnly)
            return StorageStatus.NotSupported;

        if (nodeHandle <= 0)
            return StorageStatus.InvalidArgument;

        int fileIndex = nodeHandle - 1;
        if (fileIndex < 0 || fileIndex >= _fileCount)
            return StorageStatus.NotFound;

        // Allocate a token
        for (int i = 0; i < MaxTokens; i++)
        {
            if (!_tokenOpen[i])
            {
                _tokenOpen[i] = true;
                _readOffsets[i] = 0;
                _tokenFileIndex[i] = fileIndex;
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

        int fileIndex = _tokenFileIndex[fileToken];
        return Ramdisk.Read(fileIndex, buffer, offset, count);
    }

    public override int Seek(int fileToken, long absoluteOffset)
    {
        if (fileToken < 0 || fileToken >= MaxTokens || !_tokenOpen[fileToken])
            return StorageStatus.InvalidArgument;

        _readOffsets[fileToken] = absoluteOffset;
        return StorageStatus.Ok;
    }

    public override int Close(int fileToken)
    {
        if (fileToken < 0 || fileToken >= MaxTokens)
            return StorageStatus.InvalidArgument;

        _tokenOpen[fileToken] = false;
        _readOffsets[fileToken] = 0;
        _tokenFileIndex[fileToken] = 0;
        return StorageStatus.Ok;
    }
}
