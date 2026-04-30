using Zapada.Drivers.Hal;

namespace Zapada.Drivers.Usb;

internal static class UsbBitOps
{
    public static int Low32(long value)
    {
        return (int)(value & 0xFFFFFFFFL);
    }

    public static int High32(long value)
    {
        return (int)((value >> 32) & 0xFFFFFFFFL);
    }

    public static void Write64(DmaBuffer buffer, int offset, long value)
    {
        buffer.Write32(offset, Low32(value));
        buffer.Write32(offset + 4, High32(value));
    }

    public static long ReadLe32(byte[] buffer, int offset)
    {
        return ((long)buffer[offset] & 0xFFL)
            | (((long)buffer[offset + 1] & 0xFFL) << 8)
            | (((long)buffer[offset + 2] & 0xFFL) << 16)
            | (((long)buffer[offset + 3] & 0xFFL) << 24);
    }

    public static int ReadLe16(byte[] buffer, int offset)
    {
        return (buffer[offset] & 0xFF) | ((buffer[offset + 1] & 0xFF) << 8);
    }

    public static long ReadBe32(byte[] buffer, int offset)
    {
        return (((long)buffer[offset] & 0xFFL) << 24)
            | (((long)buffer[offset + 1] & 0xFFL) << 16)
            | (((long)buffer[offset + 2] & 0xFFL) << 8)
            | ((long)buffer[offset + 3] & 0xFFL);
    }

    public static int ReadBe16(byte[] buffer, int offset)
    {
        return ((buffer[offset] & 0xFF) << 8) | (buffer[offset + 1] & 0xFF);
    }

    public static void WriteLe32(DmaBuffer buffer, int offset, int value)
    {
        buffer.Write32(offset, value);
    }

    public static void WriteLe16(DmaBuffer buffer, int offset, int value)
    {
        buffer.Write16(offset, value);
    }

    public static void WriteByteArray(byte[] buffer, int offset, int value)
    {
        buffer[offset] = (byte)(value & 0xFF);
    }

    public static void WriteBe32ToArray(byte[] buffer, int offset, long value)
    {
        buffer[offset] = (byte)((value >> 24) & 0xFF);
        buffer[offset + 1] = (byte)((value >> 16) & 0xFF);
        buffer[offset + 2] = (byte)((value >> 8) & 0xFF);
        buffer[offset + 3] = (byte)(value & 0xFF);
    }

    public static void WriteBe16ToArray(byte[] buffer, int offset, int value)
    {
        buffer[offset] = (byte)((value >> 8) & 0xFF);
        buffer[offset + 1] = (byte)(value & 0xFF);
    }
}
