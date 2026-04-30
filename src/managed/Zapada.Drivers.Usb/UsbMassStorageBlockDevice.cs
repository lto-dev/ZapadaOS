using Zapada.Storage;

namespace Zapada.Drivers.Usb;

internal sealed class UsbMassStorageBlockDevice : BlockDevice
{
    private readonly BlockDeviceInfo _info = new BlockDeviceInfo();
    private XhciController _controller;

    public int Initialize(string name, XhciController controller)
    {
        if (name == null || name.Length == 0 || controller == null || controller.SectorCount <= 0)
            return StorageStatus.InvalidArgument;

        _controller = controller;
        _info.Initialize(name, "usb-storage", XhciConstants.SectorSize, controller.SectorCount, true, false);
        return StorageStatus.Ok;
    }

    public override BlockDeviceInfo GetInfo()
    {
        return _info;
    }

    public override int ReadSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset)
    {
        if (_controller == null)
            return StorageStatus.IoError;

        return _controller.ReadSectors(lba, sectorCount, buffer, bufferOffset);
    }

    public override int WriteSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset)
    {
        if (_controller == null)
            return StorageStatus.IoError;

        return _controller.WriteSectors(lba, sectorCount, buffer, bufferOffset);
    }
}
