using System.Runtime.CompilerServices;

namespace Zapada.Storage;

internal static class Ramdisk
{
    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int FileCount();

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int Lookup(string filename);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int Read(int fileIndex, byte[] buffer, int offset, int count);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern string GetFileName(int fileIndex);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int GetFileSize(int fileIndex);
}
