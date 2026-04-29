namespace Zapada.Fs.Ext;

public sealed class ExtBlockGroupDescriptor
{
    public long InodeTableBlock;

    public bool ReadFrom(byte[] buffer, int offset, bool has64Bit)
    {
        long inodeTableLow = ExtLittleEndian.ReadUInt32(buffer, offset + 8);
        long inodeTableHigh = 0;
        if (has64Bit)
            inodeTableHigh = ExtLittleEndian.ReadUInt32(buffer, offset + 0x28);

        InodeTableBlock = inodeTableLow | (inodeTableHigh << 32);
        return InodeTableBlock > 0;
    }
}
