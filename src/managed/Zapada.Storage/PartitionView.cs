namespace Zapada.Storage;

public abstract class PartitionView
{
    public abstract long GetStartLba();
    public abstract long GetSectorCount();
    public abstract int GetSchemeKind();

    public abstract int ReadSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset);
}

