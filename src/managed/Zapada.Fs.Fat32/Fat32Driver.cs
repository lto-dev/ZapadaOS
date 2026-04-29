using Zapada.Storage;

namespace Zapada.Fs.Fat32;

public static class Fat32Driver
{
    public static MountedVolume Mount(PartitionView partition)
    {
        if (partition == null)
            return null;

        Fat32Probe probe = new Fat32Probe();
        if (probe.Score(partition) <= 0)
            return null;

        return probe.Mount(partition);
    }
}
