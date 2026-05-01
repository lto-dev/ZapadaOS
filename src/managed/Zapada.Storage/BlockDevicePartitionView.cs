namespace Zapada.Storage;

public sealed class BlockDevicePartitionView : PartitionView
{
    private const bool EnableReadDiagnostics = false;

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

        long deviceLba = _startLba + lba;
        LogReadCheckpoint("partition-read-call", lba, deviceLba, sectorCount, 0);
        int rc = _device.ReadSectors(deviceLba, sectorCount, buffer, bufferOffset);
        LogReadCheckpoint("partition-read-ret", lba, deviceLba, sectorCount, rc);
        return rc;
    }

    private static void LogReadCheckpoint(string phase, long partitionLba, long deviceLba, int sectorCount, int result)
    {
        if (!EnableReadDiagnostics)
            return;

        if (partitionLba < 2048
            || (partitionLba >= 20000L && partitionLba <= 26000L)
            || (deviceLba >= 22000L && deviceLba <= 28000L)
            || (partitionLba % 512) == 0)
        {
            System.Console.Write("[BlockDevicePartitionView] ");
            System.Console.Write(phase);
            System.Console.Write(" partitionLba=");
            System.Console.Write(partitionLba);
            System.Console.Write(" deviceLba=");
            System.Console.Write(deviceLba);
            System.Console.Write(" sectors=");
            System.Console.Write(sectorCount);
            System.Console.Write(" result=");
            System.Console.Write(result);
            System.Console.Write("\n");
        }
    }
}
