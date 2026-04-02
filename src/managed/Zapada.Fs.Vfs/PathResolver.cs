namespace Zapada.Fs.Vfs;

/// <summary>
/// Path resolution helper for the VFS layer.
/// Resolves an absolute path against the mount table and returns the mount slot
/// and the path component relative to that mount point.
/// Generic mount-prefix resolver. No filesystem-specific path rules.
/// </summary>
internal static class PathResolver
{
    /// <summary>
    /// Resolve a path to a mount slot.
    /// Returns the mount slot, or -1 if no mount handles the path.
    /// </summary>
    public static int Resolve(string path)
    {
        if (path == null || path.Length == 0 || !path.StartsWith("/"))
            return -1;

        return MountTable.Resolve(path);
    }

    /// <summary>
    /// Convert a path string to an 8.3 FAT filename for directory scanning.
    /// Only the final component (after the last '/') is converted.
    /// The result is an 11-byte uppercase 8.3 padded name.
    /// Returns null if the name is not representable as 8.3.
    /// </summary>
    public static string? ToFat83(string path)
    {
        // Extract final component
        int lastSlash = -1;
        for (int i = path.Length - 1; i >= 0; i--)
        {
            if (path[i] == '/')
            {
                lastSlash = i;
                break;
            }
        }

        string name = lastSlash >= 0 ? path.Substring(lastSlash + 1) : path;
        if (name.Length == 0)
            return null;

        // Split at the last dot
        int dot = -1;
        for (int i = name.Length - 1; i >= 0; i--)
        {
            if (name[i] == '.')
            {
                dot = i;
                break;
            }
        }

        string basePart = dot >= 0 ? name.Substring(0, dot) : name;
        string extPart  = dot >= 0 ? name.Substring(dot + 1) : "";

        if (basePart.Length > 8 || extPart.Length > 3)
            return null;

        // Build padded 11-char result
        char[] result = new char[11];
        for (int i = 0; i < 11; i++)
            result[i] = ' ';

        for (int i = 0; i < basePart.Length; i++)
            result[i] = ToUpper(basePart[i]);

        for (int i = 0; i < extPart.Length; i++)
            result[8 + i] = ToUpper(extPart[i]);

        return new string(result);
    }

    private static char ToUpper(char c)
    {
        if (c >= 'a' && c <= 'z')
            return (char)(c - 32);
        return c;
    }
}

