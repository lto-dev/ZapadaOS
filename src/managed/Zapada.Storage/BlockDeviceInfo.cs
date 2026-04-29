namespace Zapada.Storage;

public sealed class BlockDeviceInfo
{
    public string Name = "";
    public string DriverKey = "";
    public int SectorSize;
    public long SectorCount;
    public bool Removable;
    public bool ReadOnly;

    public void Initialize(string name, string driverKey, int sectorSize, long sectorCount, bool removable, bool readOnly)
    {
        Name = name;
        DriverKey = driverKey;
        SectorSize = sectorSize;
        SectorCount = sectorCount;
        Removable = removable;
        ReadOnly = readOnly;
    }
}
