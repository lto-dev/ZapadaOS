using System.Runtime.CompilerServices;

namespace Zapada;

public static class BlockDev
{
    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern long SectorCount();

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int DeviceCount();

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern long SectorCountForDevice(int deviceIndex);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int ReadSector(long lba, int count, int[] buf);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int WriteSector(long lba, int count, int[] buf);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int ReadSectorForDevice(int deviceIndex, long lba, int count, int[] buf);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int WriteSectorForDevice(int deviceIndex, long lba, int count, int[] buf);
}
