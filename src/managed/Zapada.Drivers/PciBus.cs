namespace Zapada.Drivers.Hal;

public static class PciBus
{
    private const int InfoBufferLength = 32;

    public static int DeviceCount()
    {
        return DriverHal.PciDeviceCount();
    }

    public static PciDeviceInfo GetDevice(int index)
    {
        byte[] buffer = new byte[InfoBufferLength];
        int rc = DriverHal.PciGetDeviceInfo(index, buffer);
        if (rc < 0)
            return null;

        PciDeviceInfo info = new PciDeviceInfo();
        info.Handle = ReadInt32(buffer, 0);
        info.VendorId = ReadInt32(buffer, 4);
        info.DeviceId = ReadInt32(buffer, 8);
        info.ClassCode = ReadInt32(buffer, 12);
        info.Subclass = ReadInt32(buffer, 16);
        info.ProgIf = ReadInt32(buffer, 20);
        info.HeaderType = ReadInt32(buffer, 24);
        info.Bar0 = ReadInt32(buffer, 28);
        return info;
    }

    public static PciDeviceInfo FindDevice(int vendorId, int deviceId)
    {
        int count = DeviceCount();
        for (int i = 0; i < count; i++)
        {
            PciDeviceInfo info = GetDevice(i);
            if (info == null)
                continue;

            if (info.VendorId == vendorId && info.DeviceId == deviceId)
                return info;
        }

        return null;
    }

    public static MmioRegion OpenBar(int deviceHandle, int barIndex)
    {
        return MmioRegion.OpenPciBar(deviceHandle, barIndex);
    }

    private static int ReadInt32(byte[] buffer, int offset)
    {
        return (buffer[offset] & 0xFF)
             | ((buffer[offset + 1] & 0xFF) << 8)
             | ((buffer[offset + 2] & 0xFF) << 16)
             | ((buffer[offset + 3] & 0xFF) << 24);
    }
}
