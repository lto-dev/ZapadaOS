namespace Zapada.Drivers.Hal;

public sealed class MmioRegion
{
    public int Handle;
    public int Size;

    public static MmioRegion OpenPciBar(int deviceHandle, int barIndex)
    {
        int handle = DriverHal.PciOpenBar(deviceHandle, barIndex);
        if (handle <= 0)
            return null;

        int size = DriverHal.MmioRegionSize(handle);
        if (size <= 0)
        {
            DriverHal.CloseMmioRegion(handle);
            return null;
        }

        MmioRegion region = new MmioRegion();
        region.Handle = handle;
        region.Size = size;
        return region;
    }

    public int Read32(int offset)
    {
        return DriverHal.MmioRegionRead32(Handle, offset);
    }

    public int Write32(int offset, int value)
    {
        return DriverHal.MmioRegionWrite32(Handle, offset, value);
    }

    public int Close()
    {
        int handle = Handle;
        Handle = 0;
        Size = 0;
        return DriverHal.CloseMmioRegion(handle);
    }
}
