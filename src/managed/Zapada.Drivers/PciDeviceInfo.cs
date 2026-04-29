namespace Zapada.Drivers.Hal;

public sealed class PciDeviceInfo
{
    public int Handle;
    public int VendorId;
    public int DeviceId;
    public int ClassCode;
    public int Subclass;
    public int ProgIf;
    public int HeaderType;
    public int Bar0;
}
