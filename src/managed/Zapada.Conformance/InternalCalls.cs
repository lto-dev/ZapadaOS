using System.Runtime.CompilerServices;

namespace Zapada.Conformance.Runtime;

internal static class InternalCalls
{
    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern void Write(string s);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern void WriteInt(int n);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern void GcCollect();

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern long GcGetTotalMemory(bool forceFullCollection);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int GcGetFreeBytes();

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern void GcPin(object obj);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern void GcUnpin(object obj);
}
