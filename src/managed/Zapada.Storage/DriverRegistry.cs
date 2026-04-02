namespace Zapada.Storage;

public static class DriverRegistry
{
    private const int MaxProbes = 16;
    private static VolumeProbe[] s_probes = null!;
    private static int s_count;

    public static void Initialize()
    {
        s_probes = new VolumeProbe[MaxProbes];
        s_count = 0;
    }

    public static int Register(VolumeProbe probe)
    {
        if (probe == null)
            return StorageStatus.InvalidArgument;

        if (s_probes == null)
            return StorageStatus.NotMounted;

        if (s_count >= MaxProbes)
            return StorageStatus.TableFull;

        for (int i = 0; i < s_count; i++)
        {
            if (s_probes[i].GetDriverKey() == probe.GetDriverKey())
                return StorageStatus.AlreadyExists;
        }

        s_probes[s_count] = probe;
        s_count++;
        return StorageStatus.Ok;
    }

    public static VolumeProbe? FindBestProbe(PartitionView partition)
    {
        if (partition == null || s_probes == null)
            return null;

        VolumeProbe? best = null;
        int bestScore = 0;

        for (int i = 0; i < s_count; i++)
        {
            int score = s_probes[i].Score(partition);
            if (score > bestScore)
            {
                bestScore = score;
                best = s_probes[i];
            }
        }

        return best;
    }

    public static int GetProbeCount()
    {
        return s_count;
    }
}

