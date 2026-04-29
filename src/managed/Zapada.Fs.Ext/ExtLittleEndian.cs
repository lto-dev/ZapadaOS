namespace Zapada.Fs.Ext;

public static class ExtLittleEndian
{
    public static int ReadByte(byte[] buffer, int offset)
    {
        return buffer[offset] & 0xFF;
    }

    public static int ReadUInt16(byte[] buffer, int offset)
    {
        return ReadByte(buffer, offset) | (ReadByte(buffer, offset + 1) << 8);
    }

    public static int ReadInt32(byte[] buffer, int offset)
    {
        return ReadByte(buffer, offset)
             | (ReadByte(buffer, offset + 1) << 8)
             | (ReadByte(buffer, offset + 2) << 16)
             | (ReadByte(buffer, offset + 3) << 24);
    }

    public static long ReadUInt32(byte[] buffer, int offset)
    {
        return (long)ReadInt32(buffer, offset) & 0xFFFFFFFFL;
    }

    public static long ReadUInt64(byte[] buffer, int offset)
    {
        long lo = ReadUInt32(buffer, offset);
        long hi = ReadUInt32(buffer, offset + 4);
        return lo | (hi << 32);
    }
}
