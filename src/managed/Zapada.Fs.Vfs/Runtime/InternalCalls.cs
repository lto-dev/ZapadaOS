using System.Runtime.CompilerServices;

namespace Zapada.Fs.Vfs.Runtime;

internal static class InternalCalls
{
    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int Publish(string path, byte[] image);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int PublishBegin(string path, int size);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int PublishAppend(string path, byte[] chunk, int count);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int PublishEnd(string path);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int GetTickCount();
}
