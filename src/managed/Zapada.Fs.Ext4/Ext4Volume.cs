using Zapada.Fs.Ext;
using Zapada.Storage;

namespace Zapada.Fs.Ext4;

internal sealed class Ext4Volume : MountedVolume
{
    private const int RootInodeNumber = 2;
    private const int MaxTokens = 8;
    private const int MaxNodes = 64;

    private ExtVolumeReader _reader = null!;
    private ExtSuperBlock _superBlock = null!;

    private bool[] _nodeUsed = null!;
    private int[] _nodeInodeNumber = null!;
    private int[] _nodeKind = null!;
    private long[] _nodeSize = null!;
    private int[] _nodeModifiedTime = null!;
    private string[] _nodeName = null!;

    private bool[] _tokenOpen = null!;
    private int[] _tokenNodeHandle = null!;
    private ExtInode[] _tokenInode = null!;
    private long[] _tokenOffset = null!;

    public int Initialize(PartitionView partition)
    {
        if (partition == null)
            return StorageStatus.InvalidArgument;

        _reader = new ExtVolumeReader();
        int rc = _reader.Initialize(partition);
        if (rc != StorageStatus.Ok)
            return rc;

        _superBlock = _reader.SuperBlock;

        ExtInode root = new ExtInode();
        rc = _reader.ReadInode(RootInodeNumber, root);
        if (rc != StorageStatus.Ok)
            return rc;
        if (!root.IsDirectory())
            return StorageStatus.CorruptedData;

        _nodeUsed = new bool[MaxNodes];
        _nodeInodeNumber = new int[MaxNodes];
        _nodeKind = new int[MaxNodes];
        _nodeSize = new long[MaxNodes];
        _nodeModifiedTime = new int[MaxNodes];
        _nodeName = new string[MaxNodes];

        _tokenOpen = new bool[MaxTokens];
        _tokenNodeHandle = new int[MaxTokens];
        _tokenInode = new ExtInode[MaxTokens];
        _tokenOffset = new long[MaxTokens];

        _nodeUsed[0] = true;
        _nodeInodeNumber[0] = RootInodeNumber;
        _nodeKind[0] = 2;
        _nodeSize[0] = root.Size;
        _nodeModifiedTime[0] = root.ModificationTime;
        _nodeName[0] = "/";

        return StorageStatus.Ok;
    }

    public override string GetDriverKey() { return "ext4"; }
    public override string GetDisplayName() { return "Ext4 read-only volume"; }
    public override string GetVolumeLabel() { return _superBlock.VolumeName; }
    public override string GetVolumeId() { return "ext4"; }

    public override int GetRoot()
    {
        return 0;
    }

    public override int Resolve(string path)
    {
        if (path == null)
            return StorageStatus.InvalidArgument;

        int pathLen = path.Length;
        int pos = 0;
        while (pos < pathLen && path[pos] == '/')
            pos++;

        if (pos >= pathLen)
            return 0;

        int currentHandle = 0;
        int currentInodeNumber = RootInodeNumber;
        ExtInode currentInode = new ExtInode();
        int rc = _reader.ReadInode(currentInodeNumber, currentInode);
        if (rc != StorageStatus.Ok)
            return rc;

        while (pos < pathLen)
        {
            int componentStart = pos;
            while (pos < pathLen && path[pos] != '/')
                pos++;

            int componentLength = pos - componentStart;
            while (pos < pathLen && path[pos] == '/')
                pos++;

            if (componentLength <= 0)
                continue;

            ExtDirectoryRecord record = new ExtDirectoryRecord();
            rc = _reader.FindEntryInDirectory(currentInode, path, componentStart, componentLength, record);
            if (rc != StorageStatus.Ok)
                return rc;

            ExtInode child = new ExtInode();
            rc = _reader.ReadInode(record.InodeNumber, child);
            if (rc != StorageStatus.Ok)
                return rc;

            int childKind = KindFromInode(child, record.FileType);
            if (pos < pathLen && childKind != 2)
                return StorageStatus.NotDirectory;

            currentHandle = CacheNode(record.InodeNumber, childKind, child.Size, child.ModificationTime, path, componentStart, componentLength);
            if (currentHandle < 0)
                return currentHandle;

            currentInode = child;
            currentInodeNumber = record.InodeNumber;
        }

        return currentHandle;
    }

    public override int Open(int nodeHandle, int accessIntent)
    {
        if (accessIntent != FileAccessIntent.ReadOnly)
            return StorageStatus.NotSupported;
        if (!IsValidNodeHandle(nodeHandle))
            return StorageStatus.NotFound;
        if (_nodeKind[nodeHandle] != 1)
            return StorageStatus.NotFile;

        ExtInode inode = new ExtInode();
        int rc = _reader.ReadInode(_nodeInodeNumber[nodeHandle], inode);
        if (rc != StorageStatus.Ok)
            return rc;

        for (int i = 0; i < MaxTokens; i++)
        {
            if (!_tokenOpen[i])
            {
                _tokenOpen[i] = true;
                _tokenNodeHandle[i] = nodeHandle;
                _tokenInode[i] = inode;
                _tokenOffset[i] = 0;
                return i;
            }
        }

        return StorageStatus.TableFull;
    }

    public override int Read(int fileToken, byte[] buffer, int offset, int count)
    {
        if (!IsValidToken(fileToken) || buffer == null)
            return StorageStatus.InvalidArgument;
        if (offset < 0 || count < 0 || offset + count > buffer.Length)
            return StorageStatus.InvalidArgument;
        if (count == 0)
            return 0;

        ExtInode inode = _tokenInode[fileToken];
        int bytesRead = _reader.ReadFile(inode, _tokenOffset[fileToken], buffer, offset, count);
        if (bytesRead > 0)
            _tokenOffset[fileToken] += bytesRead;

        return bytesRead;
    }

    public override int Seek(int fileToken, long absoluteOffset)
    {
        if (!IsValidToken(fileToken))
            return StorageStatus.InvalidArgument;
        if (absoluteOffset < 0 || absoluteOffset > _tokenInode[fileToken].Size)
            return StorageStatus.InvalidArgument;

        _tokenOffset[fileToken] = absoluteOffset;
        return StorageStatus.Ok;
    }

    public override int Close(int fileToken)
    {
        if (fileToken < 0 || fileToken >= MaxTokens)
            return StorageStatus.InvalidArgument;

        _tokenOpen[fileToken] = false;
        _tokenNodeHandle[fileToken] = 0;
        _tokenInode[fileToken] = null!;
        _tokenOffset[fileToken] = 0;
        return StorageStatus.Ok;
    }

    public override int Stat(int nodeHandle, NodeFacts facts)
    {
        if (facts == null)
            return StorageStatus.InvalidArgument;
        if (!IsValidNodeHandle(nodeHandle))
            return StorageStatus.NotFound;

        facts.NodeHandle = nodeHandle;
        facts.NodeKind = _nodeKind[nodeHandle];
        facts.Size = _nodeSize[nodeHandle];
        facts.ModifiedTime = _nodeModifiedTime[nodeHandle];
        facts.Permissions = 0;
        return StorageStatus.Ok;
    }

    public override int ListDirectory(int nodeHandle, DirectoryEntrySink sink)
    {
        if (sink == null)
            return StorageStatus.InvalidArgument;
        if (!IsValidNodeHandle(nodeHandle))
            return StorageStatus.NotFound;
        if (_nodeKind[nodeHandle] != 2)
            return StorageStatus.NotDirectory;

        ExtInode directory = new ExtInode();
        int rc = _reader.ReadInode(_nodeInodeNumber[nodeHandle], directory);
        if (rc != StorageStatus.Ok)
            return rc;

        byte[] block = new byte[_superBlock.BlockSize];
        ExtDirectoryRecord record = new ExtDirectoryRecord();
        long bytesRemaining = directory.Size;
        long logicalBlock = 0;

        while (bytesRemaining > 0)
        {
            rc = _reader.ReadMappedBlock(directory, logicalBlock, block);
            if (rc != StorageStatus.Ok)
                return rc;

            int limit = _superBlock.BlockSize;
            if (bytesRemaining < limit)
                limit = (int)bytesRemaining;

            int pos = 0;
            while (pos + 8 <= limit)
            {
                if (!record.ReadFrom(block, pos, limit))
                    return StorageStatus.CorruptedData;

                if (record.RecordLength == 0)
                    return StorageStatus.CorruptedData;

                if (record.InodeNumber != 0 && !record.IsDotName(block, pos))
                {
                    ExtInode child = new ExtInode();
                    rc = _reader.ReadInode(record.InodeNumber, child);
                    if (rc != StorageStatus.Ok)
                        return rc;

                    string name = record.ReadName(block, pos);
                    int kind = KindFromInode(child, record.FileType);
                    int childHandle = CacheNode(record.InodeNumber, kind, child.Size, child.ModificationTime, name);
                    if (childHandle < 0)
                        return childHandle;

                    sink.OnEntry(childHandle, name, kind);
                }

                pos += record.RecordLength;
            }

            logicalBlock++;
            bytesRemaining -= _superBlock.BlockSize;
        }

        return StorageStatus.Ok;
    }

    private bool IsValidNodeHandle(int nodeHandle)
    {
        return nodeHandle >= 0 && nodeHandle < MaxNodes && _nodeUsed[nodeHandle];
    }

    private bool IsValidToken(int fileToken)
    {
        return fileToken >= 0 && fileToken < MaxTokens && _tokenOpen[fileToken];
    }

    private int CacheNode(int inodeNumber, int kind, long size, int modifiedTime, string name)
    {
        for (int i = 0; i < MaxNodes; i++)
        {
            if (_nodeUsed[i] && _nodeInodeNumber[i] == inodeNumber)
                return i;
        }

        for (int i = 1; i < MaxNodes; i++)
        {
            if (!_nodeUsed[i])
            {
                _nodeUsed[i] = true;
                _nodeInodeNumber[i] = inodeNumber;
                _nodeKind[i] = kind;
                _nodeSize[i] = size;
                _nodeModifiedTime[i] = modifiedTime;
                _nodeName[i] = name;
                return i;
            }
        }

        return StorageStatus.TableFull;
    }

    private int CacheNode(int inodeNumber, int kind, long size, int modifiedTime, string path, int componentStart, int componentLength)
    {
        string name = "";
        for (int i = 0; i < componentLength; i++)
            name = string.Concat(name, ExtText.ByteToString(path[componentStart + i]));

        return CacheNode(inodeNumber, kind, size, modifiedTime, name);
    }

    private int KindFromInode(ExtInode inode, int directoryFileType)
    {
        if (inode.IsDirectory() || directoryFileType == ExtDirectoryRecord.FileTypeDirectory)
            return 2;
        if (inode.IsRegularFile() || inode.IsSymlink() || directoryFileType == ExtDirectoryRecord.FileTypeRegularFile || directoryFileType == ExtDirectoryRecord.FileTypeSymlink)
            return 1;
        return 0;
    }
}
