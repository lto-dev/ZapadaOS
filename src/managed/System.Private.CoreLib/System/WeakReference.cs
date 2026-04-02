using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;

namespace System;

internal static class WeakReferenceHandleTags
{
    internal const nint TracksResurrectionBit = 1;
    internal const nint HandleTagBits = TracksResurrectionBit;
}

[Serializable]
[TypeForwardedFrom("mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089")]
public class WeakReference : ISerializable
{
    private nint _taggedHandle;

    public WeakReference(object? target)
        : this(target, false)
    {
    }

    public WeakReference(object? target, bool trackResurrection)
    {
        Create(target, trackResurrection);
    }

    [Obsolete]
    [EditorBrowsable(EditorBrowsableState.Never)]
    protected WeakReference(SerializationInfo info, StreamingContext context)
    {
        ArgumentNullException.ThrowIfNull(info);

        object? target = info.GetValue("TrackedObject", typeof(object));
        bool trackResurrection = info.GetBoolean("TrackResurrection");
        Create(target, trackResurrection);
    }

    [Obsolete]
    [EditorBrowsable(EditorBrowsableState.Never)]
    public virtual void GetObjectData(SerializationInfo info, StreamingContext context)
    {
        ArgumentNullException.ThrowIfNull(info);
        info.AddValue("TrackedObject", Target, typeof(object));
        info.AddValue("TrackResurrection", IsTrackResurrection());
    }

    public virtual bool TrackResurrection => IsTrackResurrection();

    private void Create(object? target, bool trackResurrection)
    {
        nint handle = GCHandle.InternalAlloc(target, trackResurrection ? GCHandleType.WeakTrackResurrection : GCHandleType.Weak);
        _taggedHandle = trackResurrection ? (handle | WeakReferenceHandleTags.TracksResurrectionBit) : handle;
    }

    private bool IsTrackResurrection() => (_taggedHandle & WeakReferenceHandleTags.TracksResurrectionBit) != 0;

    internal nint WeakHandle => _taggedHandle & ~WeakReferenceHandleTags.HandleTagBits;

    public virtual bool IsAlive
    {
        get
        {
            nint handle = WeakHandle;
            if (handle == 0)
            {
                return false;
            }

            bool result = GCHandle.InternalGet(handle) != null;
            GC.KeepAlive(this);
            return result;
        }
    }

    public virtual object? Target
    {
        get
        {
            nint handle = WeakHandle;
            if (handle == 0)
            {
                return null;
            }

            object? target = GCHandle.InternalGet(handle);
            GC.KeepAlive(this);
            return target;
        }
        set
        {
            nint handle = WeakHandle;
            if (handle == 0)
            {
                throw new InvalidOperationException();
            }

            GCHandle.InternalSet(handle, value);
            GC.KeepAlive(this);
        }
    }

    ~WeakReference()
    {
        nint handle = WeakHandle;
        if (handle != 0)
        {
            GCHandle.InternalFree(handle);
            _taggedHandle &= WeakReferenceHandleTags.TracksResurrectionBit;
        }
    }
}
