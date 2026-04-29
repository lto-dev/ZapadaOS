using Zapada.Fs.Ext;
using Zapada.Storage;

namespace Zapada.Fs.Ext4;

internal sealed class Ext4Probe : VolumeProbe
{
    public override string GetDriverKey()
    {
        return "ext4";
    }

    public override int Score(PartitionView partition)
    {
        if (partition == null)
            return 0;

        byte[] superBlockBytes = new byte[1024];
        int rc = partition.ReadSectors(2, 2, superBlockBytes, 0);
        if (rc < 0)
            return 0;

        ExtSuperBlock superBlock = new ExtSuperBlock();
        if (!superBlock.ReadFrom(superBlockBytes, 0))
            return 0;

        if (superBlock.HasUnsupportedReadOnlyFeatures())
            return 0;

        if (superBlock.HasExtents())
            return 120;

        return 80;
    }

    public override MountedVolume Mount(PartitionView partition)
    {
        if (partition == null)
            return null;

        Ext4Volume volume = new Ext4Volume();
        int rc = volume.Initialize(partition);
        if (rc != StorageStatus.Ok)
            return null;

        return volume;
    }
}
