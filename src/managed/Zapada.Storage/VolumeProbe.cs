namespace Zapada.Storage;

public abstract class VolumeProbe
{
    public abstract string GetDriverKey();

    /// <summary>
    /// Returns 0 when not recognized, positive value for confidence.
    /// </summary>
    public abstract int Score(PartitionView partition);

    /// <summary>
    /// Returns a mounted volume instance, or null on failure.
    /// </summary>
    public abstract MountedVolume? Mount(PartitionView partition);
}

