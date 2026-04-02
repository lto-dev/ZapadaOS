using Zapada.Storage;

namespace Zapada.Fs.Vfs;

/// <summary>
/// Static mount table for the VFS layer. Supports up to 4 mount points.
/// Each slot holds a path prefix and a mounted volume reference.
/// </summary>
internal static class MountTable
{
    private const int MaxMounts = 4;

    private static string? s_path0;
    private static string? s_path1;
    private static string? s_path2;
    private static string? s_path3;

    private static MountedVolume? s_volume0;
    private static MountedVolume? s_volume1;
    private static MountedVolume? s_volume2;
    private static MountedVolume? s_volume3;

    private static int s_count;

    /// <summary>
    /// Reset mount slots. Called explicitly by [`Vfs.Initialize()`](src/managed/Zapada.Fs.Vfs/Vfs.cs:16).
    /// </summary>
    public static void Initialize()
    {
        s_path0 = null;
        s_path1 = null;
        s_path2 = null;
        s_path3 = null;
        s_volume0 = null;
        s_volume1 = null;
        s_volume2 = null;
        s_volume3 = null;
        s_count = 0;
    }

    /// <summary>
    /// Register a mount point. Returns the slot index, or -1 if the table is full.
    /// </summary>
    public static int Mount(string path, MountedVolume volume)
    {
        if (volume == null || path == null)
            return StorageStatus.InvalidArgument;

        if (path.Length == 0 || path[0] != '/')
            return StorageStatus.InvalidArgument;

        if (s_count >= MaxMounts)
            return StorageStatus.TableFull;

        // Prevent duplicate mount path registration.
        for (int i = 0; i < s_count; i++)
        {
            string? existingPath = GetPath(i);
            if (existingPath != null && existingPath.Length == path.Length && existingPath.StartsWith(path))
                return StorageStatus.AlreadyExists;
        }

        int slot = s_count;
        //TODO: do this in new gc 
        //global::System.GC.Pin(volume);
        SetSlot(slot, path, volume);
        s_count++;
        return slot;
    }

    /// <summary>
    /// Find the mount slot whose prefix best matches the given path.
    /// Returns the slot index, or -1 if not found.
    /// </summary>
    public static int Resolve(string path)
    {
        if (path == null || path.Length == 0)
            return -1;

        int best    = -1;
        int bestLen = -1;

        for (int i = 0; i < s_count; i++)
        {
            string? prefix = GetPath(i);
            if (prefix == null)
                continue;

            int prefixLen = prefix.Length;
            if (path.Length >= prefixLen && prefixLen > bestLen)
            {
                if (!path.StartsWith(prefix))
                    continue;

                bool boundaryOkay = false;
                if (prefix.Length == 1 && prefix[0] == '/')
                {
                    boundaryOkay = true;
                }
                else if (path.Length == prefixLen)
                {
                    boundaryOkay = true;
                }
                else
                {
                    string tail = path.Substring(prefixLen);
                    if (tail.StartsWith("/"))
                        boundaryOkay = true;
                }

                if (boundaryOkay)
                {
                    best    = i;
                    bestLen = prefixLen;
                }
            }
        }

        return best;
    }

    public static string? GetPath(int slot)
    {
        if (slot < 0 || slot >= s_count)
            return null;

        if (slot == 0)
            return "/";
        if (slot == 1)
            return s_path1;
        if (slot == 2)
            return s_path2;
        if (slot == 3)
            return s_path3;

        return null;
    }

    public static MountedVolume? GetVolume(int slot)
    {
        if (slot < 0 || slot >= s_count)
            return null;

        if (slot == 0)
            return s_volume0;
        if (slot == 1)
            return s_volume1;
        if (slot == 2)
            return s_volume2;
        if (slot == 3)
            return s_volume3;

        return null;
    }

    private static void SetSlot(int slot, string path, MountedVolume volume)
    {
        switch (slot)
        {
            case 0:
                s_path0 = path;
                s_volume0 = volume;
                return;
            case 1:
                s_path1 = path;
                s_volume1 = volume;
                return;
            case 2:
                s_path2 = path;
                s_volume2 = volume;
                return;
            case 3:
                s_path3 = path;
                s_volume3 = volume;
                return;
        }
    }

    public static int Count => s_count;
}

