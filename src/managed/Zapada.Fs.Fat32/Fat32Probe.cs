using Zapada.Storage;

namespace Zapada.Fs.Fat32;

internal sealed class Fat32Probe : VolumeProbe
{
    public override string GetDriverKey()
    {
        return "fat32";
    }

    public override int Score(PartitionView partition)
    {
        if (partition == null)
            return 0;

        byte[] sector = new byte[512];
        int rc = partition.ReadSectors(0, 1, sector, 0);
        if (rc < 0)
            return 0;

        if (sector[510] != 0x55 || sector[511] != 0xAA)
            return 0;

        int bytesPerSector = sector[11] | (sector[12] << 8);
        if (bytesPerSector != 512)
            return 0;

        int sectorsPerCluster = sector[13];
        if (sectorsPerCluster <= 0 || (sectorsPerCluster & (sectorsPerCluster - 1)) != 0)
            return 0;

        if (sector[82] != (byte)'F'
            || sector[83] != (byte)'A'
            || sector[84] != (byte)'T'
            || sector[85] != (byte)'3'
            || sector[86] != (byte)'2')
        {
            return 0;
        }

        return 100;
    }

    public override MountedVolume? Mount(PartitionView partition)
    {
        if (partition == null)
            return null;

        long startLba = partition.GetStartLba();
        if (startLba < 0 || startLba > 2147483647L)
            return null;

        Fat32Volume volume = new Fat32Volume();
        int rc = volume.Initialize((int)startLba);
        if (rc != StorageStatus.Ok)
            return null;

        return volume;
    }
}

