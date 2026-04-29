namespace Zapada.Fs.Ext;

public sealed class ExtSuperBlock
{
    public const int MagicValue = 0xEF53;

    public int InodesCount;
    public long BlocksCount;
    public int FirstDataBlock;
    public int LogBlockSize;
    public int BlocksPerGroup;
    public int InodesPerGroup;
    public int InodeSize;
    public int DescriptorSize;
    public int FeatureCompat;
    public int FeatureIncompat;
    public int FeatureReadOnlyCompat;
    public int BlockSize;
    public string VolumeName = "";

    public bool ReadFrom(byte[] buffer, int offset)
    {
        int magic = ExtLittleEndian.ReadUInt16(buffer, offset + 56);
        if (magic != MagicValue)
            return false;

        InodesCount = (int)ExtLittleEndian.ReadUInt32(buffer, offset + 0);
        long blocksLow = ExtLittleEndian.ReadUInt32(buffer, offset + 4);
        long blocksHigh = ExtLittleEndian.ReadUInt32(buffer, offset + 336);
        BlocksCount = blocksLow | (blocksHigh << 32);
        FirstDataBlock = (int)ExtLittleEndian.ReadUInt32(buffer, offset + 20);
        LogBlockSize = (int)ExtLittleEndian.ReadUInt32(buffer, offset + 24);
        BlocksPerGroup = (int)ExtLittleEndian.ReadUInt32(buffer, offset + 32);
        InodesPerGroup = (int)ExtLittleEndian.ReadUInt32(buffer, offset + 40);
        FeatureCompat = (int)ExtLittleEndian.ReadUInt32(buffer, offset + 92);
        FeatureIncompat = (int)ExtLittleEndian.ReadUInt32(buffer, offset + 96);
        FeatureReadOnlyCompat = (int)ExtLittleEndian.ReadUInt32(buffer, offset + 100);
        InodeSize = ExtLittleEndian.ReadUInt16(buffer, offset + 88);
        DescriptorSize = ExtLittleEndian.ReadUInt16(buffer, offset + 254);
        VolumeName = ExtText.ReadFixedAscii(buffer, offset + 120, 16);

        if (LogBlockSize < 0 || LogBlockSize > 6)
            return false;

        BlockSize = 1024 << LogBlockSize;
        if (BlockSize < 1024 || BlockSize > 65536)
            return false;

        if (InodeSize <= 0)
            InodeSize = 128;

        if (DescriptorSize < 32)
            DescriptorSize = 32;

        if (!Has64Bit())
            DescriptorSize = 32;

        return InodesCount > 0 && BlocksCount > 0 && BlocksPerGroup > 0 && InodesPerGroup > 0;
    }

    public bool HasExtents()
    {
        return (FeatureIncompat & ExtFeatureFlags.IncompatExtents) != 0;
    }

    public bool Has64Bit()
    {
        return (FeatureIncompat & ExtFeatureFlags.IncompatSixtyFourBit) != 0;
    }

    public bool HasUnsupportedReadOnlyFeatures()
    {
        return ExtFeatureFlags.HasUnsupportedIncompat(FeatureIncompat)
            || ExtFeatureFlags.HasUnsupportedReadOnlyCompat(FeatureReadOnlyCompat);
    }

    public long GroupDescriptorTableBlock()
    {
        if (BlockSize == 1024)
            return 2;

        return 1;
    }
}
