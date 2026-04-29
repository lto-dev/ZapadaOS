namespace Zapada.Storage;

public abstract class BlockDevice
{
    public abstract BlockDeviceInfo GetInfo();
    public abstract int ReadSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset);
    public abstract int WriteSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset);
}
