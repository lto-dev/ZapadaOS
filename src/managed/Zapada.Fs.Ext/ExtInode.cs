namespace Zapada.Fs.Ext;

public sealed class ExtInode
{
    public int Mode;
    public long Size;
    public int Flags;
    public int LinksCount;
    public int ModificationTime;
    public byte[] BlockMap = new byte[60];

    public bool ReadFrom(byte[] buffer, int offset)
    {
        Mode = ExtLittleEndian.ReadUInt16(buffer, offset + 0);
        long sizeLow = ExtLittleEndian.ReadUInt32(buffer, offset + 4);
        ModificationTime = (int)ExtLittleEndian.ReadUInt32(buffer, offset + 16);
        LinksCount = ExtLittleEndian.ReadUInt16(buffer, offset + 26);
        Flags = (int)ExtLittleEndian.ReadUInt32(buffer, offset + 32);

        long sizeHigh = 0;
        if (IsRegularFile())
            sizeHigh = ExtLittleEndian.ReadUInt32(buffer, offset + 108);

        Size = sizeLow | (sizeHigh << 32);

        for (int i = 0; i < 60; i++)
            BlockMap[i] = buffer[offset + 40 + i];

        return Mode != 0 && LinksCount >= 0;
    }

    public bool IsDirectory()
    {
        return (Mode & ExtFeatureFlags.ModeFileTypeMask) == ExtFeatureFlags.ModeDirectory;
    }

    public bool IsRegularFile()
    {
        return (Mode & ExtFeatureFlags.ModeFileTypeMask) == ExtFeatureFlags.ModeRegularFile;
    }

    public bool IsSymlink()
    {
        return (Mode & ExtFeatureFlags.ModeFileTypeMask) == ExtFeatureFlags.ModeSymlink;
    }

    public bool HasExtents()
    {
        return (Flags & ExtFeatureFlags.InodeFlagExtents) != 0;
    }

    public long ReadBlockPointer(int index)
    {
        return ExtLittleEndian.ReadUInt32(BlockMap, index * 4);
    }
}
