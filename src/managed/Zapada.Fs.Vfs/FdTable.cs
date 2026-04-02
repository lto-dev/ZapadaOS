namespace Zapada.Fs.Vfs;

/// <summary>
/// Static file descriptor table for the VFS layer. Supports up to 16 open file descriptors.
/// Each entry holds only generic VFS state: mount slot, opaque volume token,
/// current offset, and cached size.
///
/// State is reset by [`Initialize()`](src/managed/Zapada.Fs.Vfs/FdTable.cs:28)
/// during boot for deterministic startup ordering.
/// </summary>
internal static class FdTable
{
    private const int MaxFds = 16;

    private static int[]  s_mountSlot;
    private static int[]  s_volumeToken;
    private static int[]  s_curOffset;
    private static int[]  s_size;
    private static bool[] s_open;

    /// <summary>
    /// Allocate internal tables. Must be called before any other FdTable operation.
    /// Called explicitly by Vfs.Initialize() because .cctor is not invoked
    /// by the bare-metal interpreter.
    /// </summary>
    public static void Initialize()
    {
        s_mountSlot    = new int[MaxFds];
        s_volumeToken  = new int[MaxFds];
        s_curOffset    = new int[MaxFds];
        s_size         = new int[MaxFds];
        s_open         = new bool[MaxFds];
    }

    /// <summary>
    /// Allocate a new file descriptor. Returns the fd index, or -1 if none available.
    /// </summary>
    public static int Alloc(int mountSlot, int volumeToken, int size)
    {
        for (int i = 0; i < MaxFds; i++)
        {
            if (!s_open[i])
            {
                s_open[i]          = true;
                s_mountSlot[i]     = mountSlot;
                s_volumeToken[i]   = volumeToken;
                s_curOffset[i]     = 0;
                s_size[i]          = size;
                return i;
            }
        }

        return -1;
    }

    /// <summary>
    /// Release a file descriptor.
    /// </summary>
    public static void Free(int fd)
    {
        if (fd >= 0 && fd < MaxFds)
            s_open[fd] = false;
    }

    public static bool IsOpen(int fd)       => fd >= 0 && fd < MaxFds && s_open[fd];
    public static int MountSlot(int fd)     => IsOpen(fd) ? s_mountSlot[fd]   : -1;
    public static int VolumeToken(int fd)   => IsOpen(fd) ? s_volumeToken[fd] : -1;
    public static int CurOffset(int fd)     => IsOpen(fd) ? s_curOffset[fd]   : -1;
    public static int Size(int fd)          => IsOpen(fd) ? s_size[fd]        : -1;

    public static void SetCurOffset(int fd, int offset)
    {
        if (IsOpen(fd))
            s_curOffset[fd] = offset;
    }
}

