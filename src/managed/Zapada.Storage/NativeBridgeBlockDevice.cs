namespace Zapada.Storage;

public sealed class NativeBridgeBlockDevice : BlockDevice
{
    private readonly BlockDeviceInfo _info;
    private readonly int _deviceIndex;

    public NativeBridgeBlockDevice(string name, string driverKey, int sectorSize, long sectorCount)
        : this(name, driverKey, sectorSize, sectorCount, 0)
    {
    }

    public NativeBridgeBlockDevice(string name, string driverKey, int sectorSize, long sectorCount, int deviceIndex)
    {
        _deviceIndex = deviceIndex;
        _info = new BlockDeviceInfo();
        _info.Initialize(name, driverKey, sectorSize, sectorCount, false, false);
    }

    public override BlockDeviceInfo GetInfo()
    {
        return _info;
    }

    public override int ReadSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset)
    {
        if (buffer == null || sectorCount <= 0 || bufferOffset < 0)
            return StorageStatus.InvalidArgument;

        if (lba < 0 || (_info.SectorCount > 0 && lba + sectorCount > _info.SectorCount))
            return StorageStatus.InvalidArgument;

        int byteCount = sectorCount * 512;
        if (bufferOffset + byteCount > buffer.Length)
            return StorageStatus.InvalidArgument;

        int[] intBuffer = new int[sectorCount * 128];
        int rc = Zapada.BlockDev.ReadSectorForDevice(_deviceIndex, lba, sectorCount, intBuffer);
        if (rc != 0)
            return StorageStatus.IoError;

        for (int i = 0; i < byteCount; i++)
            buffer[bufferOffset + i] = (byte)((intBuffer[i / 4] >> ((i % 4) * 8)) & 0xFF);

        return sectorCount;
    }

    public override int WriteSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset)
    {
        if (buffer == null || sectorCount <= 0 || bufferOffset < 0)
            return StorageStatus.InvalidArgument;

        if (lba < 0 || (_info.SectorCount > 0 && lba + sectorCount > _info.SectorCount))
            return StorageStatus.InvalidArgument;

        int byteCount = sectorCount * 512;
        if (bufferOffset + byteCount > buffer.Length)
            return StorageStatus.InvalidArgument;

        int[] intBuffer = new int[sectorCount * 128];
        for (int i = 0; i < byteCount; i++)
            intBuffer[i / 4] = intBuffer[i / 4] | ((buffer[bufferOffset + i] & 0xFF) << ((i % 4) * 8));

        int rc = Zapada.BlockDev.WriteSectorForDevice(_deviceIndex, lba, sectorCount, intBuffer);
        if (rc != 0)
            return StorageStatus.IoError;

        return sectorCount;
    }
}
