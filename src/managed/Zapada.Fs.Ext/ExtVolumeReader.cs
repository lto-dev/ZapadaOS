using Zapada.Storage;

namespace Zapada.Fs.Ext;

public sealed class ExtVolumeReader
{
    private PartitionView _partition = null!;
    private ExtSuperBlock _superBlock = null!;

    public ExtSuperBlock SuperBlock
    {
        get { return _superBlock; }
    }

    public int Initialize(PartitionView partition)
    {
        if (partition == null)
            return StorageStatus.InvalidArgument;

        byte[] superBlockBytes = new byte[1024];
        int rc = partition.ReadSectors(2, 2, superBlockBytes, 0);
        if (rc < 0)
            return StorageStatus.IoError;

        ExtSuperBlock superBlock = new ExtSuperBlock();
        if (!superBlock.ReadFrom(superBlockBytes, 0))
            return StorageStatus.CorruptedData;

        if (superBlock.HasUnsupportedReadOnlyFeatures())
            return StorageStatus.NotSupported;

        _partition = partition;
        _superBlock = superBlock;
        return StorageStatus.Ok;
    }

    public int ReadBlock(long blockNumber, byte[] buffer, int bufferOffset)
    {
        if (_partition == null || _superBlock == null)
            return StorageStatus.NotMounted;

        if (blockNumber < 0 || buffer == null || bufferOffset < 0)
            return StorageStatus.InvalidArgument;

        if (bufferOffset + _superBlock.BlockSize > buffer.Length)
            return StorageStatus.InvalidArgument;

        long byteOffset = blockNumber * (long)_superBlock.BlockSize;
        long lba = byteOffset / 512L;
        int sectorCount = _superBlock.BlockSize / 512;
        int rc = _partition.ReadSectors(lba, sectorCount, buffer, bufferOffset);
        if (rc < 0)
            return StorageStatus.IoError;

        return StorageStatus.Ok;
    }

    public int ReadInode(int inodeNumber, ExtInode inode)
    {
        if (inode == null)
            return StorageStatus.InvalidArgument;

        if (inodeNumber <= 0)
            return StorageStatus.InvalidArgument;

        if (_superBlock == null)
            return StorageStatus.NotMounted;

        int group = (inodeNumber - 1) / _superBlock.InodesPerGroup;
        int indexInGroup = (inodeNumber - 1) % _superBlock.InodesPerGroup;

        ExtBlockGroupDescriptor descriptor = new ExtBlockGroupDescriptor();
        int descRc = ReadGroupDescriptor(group, descriptor);
        if (descRc != StorageStatus.Ok)
            return descRc;

        long inodeOffset = (long)indexInGroup * (long)_superBlock.InodeSize;
        long inodeBlock = descriptor.InodeTableBlock + inodeOffset / _superBlock.BlockSize;
        int offsetInBlock = (int)(inodeOffset % _superBlock.BlockSize);

        if (offsetInBlock + _superBlock.InodeSize > _superBlock.BlockSize)
            return StorageStatus.NotSupported;

        byte[] block = new byte[_superBlock.BlockSize];
        int readRc = ReadBlock(inodeBlock, block, 0);
        if (readRc != StorageStatus.Ok)
            return readRc;

        if (!inode.ReadFrom(block, offsetInBlock))
            return StorageStatus.CorruptedData;

        return StorageStatus.Ok;
    }

    public int FindEntryInDirectory(ExtInode directory, string path, int componentStart, int componentLength, ExtDirectoryRecord result)
    {
        if (directory == null || path == null || result == null)
            return StorageStatus.InvalidArgument;
        if (!directory.IsDirectory())
            return StorageStatus.NotDirectory;

        byte[] block = new byte[_superBlock.BlockSize];
        ExtDirectoryRecord record = new ExtDirectoryRecord();
        long bytesRemaining = directory.Size;
        long logicalBlock = 0;

        while (bytesRemaining > 0)
        {
            int readRc = ReadMappedBlock(directory, logicalBlock, block);
            if (readRc != StorageStatus.Ok)
                return readRc;

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

                if (record.InodeNumber != 0 && record.NameMatches(path, componentStart, componentLength, block, pos))
                {
                    result.InodeNumber = record.InodeNumber;
                    result.RecordLength = record.RecordLength;
                    result.NameLength = record.NameLength;
                    result.FileType = record.FileType;
                    return StorageStatus.Ok;
                }

                pos += record.RecordLength;
            }

            logicalBlock++;
            bytesRemaining -= _superBlock.BlockSize;
        }

        return StorageStatus.NotFound;
    }

    public int ReadMappedBlock(ExtInode inode, long logicalBlock, byte[] block)
    {
        long physicalBlock = MapLogicalBlock(inode, logicalBlock);
        if (physicalBlock < 0)
            return (int)physicalBlock;

        if (physicalBlock == 0)
        {
            for (int i = 0; i < _superBlock.BlockSize; i++)
                block[i] = 0;
            return StorageStatus.Ok;
        }

        return ReadBlock(physicalBlock, block, 0);
    }

    public int ReadFile(ExtInode inode, long absoluteOffset, byte[] buffer, int offset, int count)
    {
        if (inode == null || buffer == null || offset < 0 || count < 0 || offset + count > buffer.Length)
            return StorageStatus.InvalidArgument;
        if (absoluteOffset < 0)
            return StorageStatus.InvalidArgument;
        if (absoluteOffset >= inode.Size || count == 0)
            return 0;

        long remainingFileBytes = inode.Size - absoluteOffset;
        if (count > remainingFileBytes)
            count = (int)remainingFileBytes;

        byte[] block = new byte[_superBlock.BlockSize];
        int written = 0;

        while (written < count)
        {
            long fileOffset = absoluteOffset + written;
            long logicalBlock = fileOffset / _superBlock.BlockSize;
            int blockOffset = (int)(fileOffset % _superBlock.BlockSize);

            int readRc = ReadMappedBlock(inode, logicalBlock, block);
            if (readRc != StorageStatus.Ok)
                return written > 0 ? written : readRc;

            int available = _superBlock.BlockSize - blockOffset;
            int wanted = count - written;
            if (wanted > available)
                wanted = available;

            for (int i = 0; i < wanted; i++)
                buffer[offset + written + i] = block[blockOffset + i];

            written += wanted;
        }

        return written;
    }

    public long MapLogicalBlock(ExtInode inode, long logicalBlock)
    {
        if (inode == null || logicalBlock < 0)
            return StorageStatus.InvalidArgument;

        if (inode.HasExtents())
            return MapExtentNode(inode.BlockMap, 0, logicalBlock, 0);

        return MapClassicBlock(inode, logicalBlock);
    }

    private int ReadGroupDescriptor(int group, ExtBlockGroupDescriptor descriptor)
    {
        if (group < 0 || descriptor == null)
            return StorageStatus.InvalidArgument;

        long tableBlock = _superBlock.GroupDescriptorTableBlock();
        long descriptorOffset = (long)group * (long)_superBlock.DescriptorSize;
        long block = tableBlock + descriptorOffset / _superBlock.BlockSize;
        int offset = (int)(descriptorOffset % _superBlock.BlockSize);

        if (offset + _superBlock.DescriptorSize > _superBlock.BlockSize)
            return StorageStatus.NotSupported;

        byte[] blockBytes = new byte[_superBlock.BlockSize];
        int readRc = ReadBlock(block, blockBytes, 0);
        if (readRc != StorageStatus.Ok)
            return readRc;

        if (!descriptor.ReadFrom(blockBytes, offset, _superBlock.Has64Bit()))
            return StorageStatus.CorruptedData;

        return StorageStatus.Ok;
    }

    private long MapClassicBlock(ExtInode inode, long logicalBlock)
    {
        if (logicalBlock < 12)
            return inode.ReadBlockPointer((int)logicalBlock);

        int pointersPerBlock = _superBlock.BlockSize / 4;
        long singleIndex = logicalBlock - 12;
        if (singleIndex >= 0 && singleIndex < pointersPerBlock)
        {
            long indirectBlock = inode.ReadBlockPointer(12);
            if (indirectBlock == 0)
                return 0;

            byte[] block = new byte[_superBlock.BlockSize];
            int readRc = ReadBlock(indirectBlock, block, 0);
            if (readRc != StorageStatus.Ok)
                return readRc;

            return ExtLittleEndian.ReadUInt32(block, (int)singleIndex * 4);
        }

        return StorageStatus.NotSupported;
    }

    private long MapExtentNode(byte[] nodeBytes, int nodeOffset, long logicalBlock, int depthGuard)
    {
        if (depthGuard > 5)
            return StorageStatus.NotSupported;

        int magic = ExtLittleEndian.ReadUInt16(nodeBytes, nodeOffset + 0);
        if (magic != 0xF30A)
            return StorageStatus.CorruptedData;

        int entries = ExtLittleEndian.ReadUInt16(nodeBytes, nodeOffset + 2);
        int depth = ExtLittleEndian.ReadUInt16(nodeBytes, nodeOffset + 6);
        if (entries < 0 || entries > 340)
            return StorageStatus.CorruptedData;

        if (depth == 0)
            return MapExtentLeaf(nodeBytes, nodeOffset, entries, logicalBlock);

        long selectedLeaf = -1;
        for (int i = 0; i < entries; i++)
        {
            int entryOffset = nodeOffset + 12 + i * 12;
            long firstLogical = ExtLittleEndian.ReadUInt32(nodeBytes, entryOffset + 0);
            if (firstLogical <= logicalBlock)
            {
                long leafLow = ExtLittleEndian.ReadUInt32(nodeBytes, entryOffset + 4);
                long leafHigh = ExtLittleEndian.ReadUInt16(nodeBytes, entryOffset + 8);
                selectedLeaf = leafLow | (leafHigh << 32);
            }
        }

        if (selectedLeaf < 0)
            return 0;

        byte[] child = new byte[_superBlock.BlockSize];
        int readRc = ReadBlock(selectedLeaf, child, 0);
        if (readRc != StorageStatus.Ok)
            return readRc;

        return MapExtentNode(child, 0, logicalBlock, depthGuard + 1);
    }

    private long MapExtentLeaf(byte[] nodeBytes, int nodeOffset, int entries, long logicalBlock)
    {
        for (int i = 0; i < entries; i++)
        {
            int entryOffset = nodeOffset + 12 + i * 12;
            long firstLogical = ExtLittleEndian.ReadUInt32(nodeBytes, entryOffset + 0);
            int blockCount = ExtLittleEndian.ReadUInt16(nodeBytes, entryOffset + 4) & 0x7FFF;
            long physicalHigh = ExtLittleEndian.ReadUInt16(nodeBytes, entryOffset + 6);
            long physicalLow = ExtLittleEndian.ReadUInt32(nodeBytes, entryOffset + 8);
            long physicalStart = physicalLow | (physicalHigh << 32);

            if (logicalBlock >= firstLogical && logicalBlock < firstLogical + blockCount)
                return physicalStart + (logicalBlock - firstLogical);
        }

        return 0;
    }
}
