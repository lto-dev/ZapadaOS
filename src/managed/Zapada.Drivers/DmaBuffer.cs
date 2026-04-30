namespace Zapada.Drivers.Hal;

public sealed class DmaBuffer
{
    public int Handle;
    public int Size;
    public long PhysicalAddress;

    public static DmaBuffer Allocate(int size)
    {
        int handle = DriverHal.AllocDmaBuffer(size);
        if (handle <= 0)
            return null;

        int actualSize = DriverHal.DmaBufferSize(handle);
        long physicalAddress = DriverHal.DmaBufferPhysicalAddress(handle);
        if (actualSize < size || physicalAddress == 0)
        {
            DriverHal.FreeDmaBuffer(handle);
            return null;
        }

        DmaBuffer buffer = new DmaBuffer();
        buffer.Handle = handle;
        buffer.Size = actualSize;
        buffer.PhysicalAddress = physicalAddress;
        return buffer;
    }

    public int Read32(int offset)
    {
        return DriverHal.DmaBufferRead32(Handle, offset);
    }

    public int Read16(int offset)
    {
        return DriverHal.DmaBufferRead16(Handle, offset);
    }

    public int Read8(int offset)
    {
        return DriverHal.DmaBufferRead8(Handle, offset);
    }

    public int Write32(int offset, int value)
    {
        return DriverHal.DmaBufferWrite32(Handle, offset, value);
    }

    public int Write16(int offset, int value)
    {
        return DriverHal.DmaBufferWrite16(Handle, offset, value);
    }

    public int Write8(int offset, int value)
    {
        return DriverHal.DmaBufferWrite8(Handle, offset, value);
    }

    public int CopyFrom(int offset, byte[] source, int sourceOffset, int count)
    {
        return DriverHal.DmaBufferCopyFrom(Handle, offset, source, sourceOffset, count);
    }

    public int CopyTo(int offset, byte[] destination, int destinationOffset, int count)
    {
        return DriverHal.DmaBufferCopyTo(Handle, offset, destination, destinationOffset, count);
    }

    public int Free()
    {
        int handle = Handle;
        Handle = 0;
        Size = 0;
        PhysicalAddress = 0;
        return DriverHal.FreeDmaBuffer(handle);
    }
}
