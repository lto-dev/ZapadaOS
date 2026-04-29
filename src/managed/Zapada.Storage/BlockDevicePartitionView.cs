namespace Zapada.Storage;

public sealed class BlockDevicePartitionView : PartitionView
{
    private BlockDevice? _device;
    private long _startLba;
    private long _sectorCount;
    private int _schemeKind;
    private string _name = "";

    public void Initialize(BlockDevice device, string name, long startLba, long sectorCount, int schemeKind)
    {
        _device = device;
        _name = name;
        _startLba = startLba;
        _sectorCount = sectorCount;
        _schemeKind = schemeKind;
    }

    public string GetName() { return _name; }
    public BlockDevice? GetDevice() { return _device; }
    public override long GetStartLba() { return _startLba; }
    public override long GetSectorCount() { return _sectorCount; }
    public override int GetSchemeKind() { return _schemeKind; }

    public override int ReadSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset)
    {
        if (_device == null || buffer == null || sectorCount <= 0 || bufferOffset < 0)
            return StorageStatus.InvalidArgument;

        if (lba < 0 || lba + sectorCount > _sectorCount)
            return StorageStatus.InvalidArgument;

        return _device.ReadSectors(_startLba + lba, sectorCount, buffer, bufferOffset);
    }
}
