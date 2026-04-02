using System.Runtime.CompilerServices;

namespace System;

public enum GCCollectionMode
{
    Default = 0,
    Forced = 1,
    Optimized = 2,
    Aggressive = 3,
}

public enum GCNotificationStatus
{
    Succeeded = 0,
    Failed = 1,
    Canceled = 2,
    Timeout = 3,
    NotApplicable = 4,
}

internal enum InternalGCCollectionMode
{
    NonBlocking = 0x00000001,
    Blocking = 0x00000002,
    Optimized = 0x00000004,
    Compacting = 0x00000008,
    Aggressive = 0x00000010,
}

public static partial class GC
{
    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void _Collect(int generation, int mode, bool lowMemoryPressure);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern int GetMaxGeneration();

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern int _CollectionCount(int generation, int getSpecialGCCount);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern int GetGenerationInternal(object obj);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void WaitForPendingFinalizers();

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void SuppressFinalizeInternal(object o);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void _ReRegisterForFinalize(object o);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern long GetTotalMemory(bool forceFullCollection);

    public static int MaxGeneration => GetMaxGeneration();

    public static int GetGeneration(object obj)
    {
        ArgumentNullException.ThrowIfNull(obj);
        return GetGenerationInternal(obj);
    }

    public static void Collect(int generation)
    {
        Collect(generation, GCCollectionMode.Default);
    }

    public static void Collect()
    {
        _Collect(-1, (int)InternalGCCollectionMode.Blocking, lowMemoryPressure: false);
    }

    public static void Collect(int generation, GCCollectionMode mode)
    {
        Collect(generation, mode, blocking: true);
    }

    public static void Collect(int generation, GCCollectionMode mode, bool blocking)
    {
        bool aggressive = generation == MaxGeneration && mode == GCCollectionMode.Aggressive;
        Collect(generation, mode, blocking, compacting: aggressive);
    }

    public static void Collect(int generation, GCCollectionMode mode, bool blocking, bool compacting)
    {
        Collect(generation, mode, blocking, compacting, lowMemoryPressure: false);
    }

    internal static void Collect(int generation, GCCollectionMode mode, bool blocking, bool compacting, bool lowMemoryPressure)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(generation);

        if ((mode < GCCollectionMode.Default) || (mode > GCCollectionMode.Aggressive))
        {
            throw new ArgumentOutOfRangeException(nameof(mode));
        }

        int internalModes = 0;

        if (mode == GCCollectionMode.Optimized)
        {
            internalModes |= (int)InternalGCCollectionMode.Optimized;
        }
        else if (mode == GCCollectionMode.Aggressive)
        {
            internalModes |= (int)InternalGCCollectionMode.Aggressive;
        }

        if (compacting)
        {
            internalModes |= (int)InternalGCCollectionMode.Compacting;
        }

        if (blocking)
        {
            internalModes |= (int)InternalGCCollectionMode.Blocking;
        }
        else if (!compacting)
        {
            internalModes |= (int)InternalGCCollectionMode.NonBlocking;
        }

        _Collect(generation, internalModes, lowMemoryPressure);
    }

    public static int CollectionCount(int generation)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(generation);
        return _CollectionCount(generation, 0);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public static void KeepAlive(object? obj)
    {
    }

    public static void SuppressFinalize(object obj)
    {
        ArgumentNullException.ThrowIfNull(obj);
        SuppressFinalizeInternal(obj);
    }

    public static void ReRegisterForFinalize(object obj)
    {
        ArgumentNullException.ThrowIfNull(obj);
        _ReRegisterForFinalize(obj);
    }
}
