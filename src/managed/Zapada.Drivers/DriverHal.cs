using System.Runtime.CompilerServices;

namespace Zapada.Drivers.Hal;

public static class DriverHal
{
    public const int StatusSuccess = 0;
    public const int StatusInvalid = -1;
    public const int StatusUnsupported = -2;

    public const int IpcMessagePayloadMax = 64;
    public const int IpcMessageAny = 0;
    public const int IpcMessageData = 2;
    public const int IpcMessageUser = 0x00010000;
    public const int IpcMessageIrq = IpcMessageUser + 0x100;
    public const int IpcErrEmpty = -3;

    public const int IrqTimer = 0;

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int CreateChannel();

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int DestroyChannel(int handle);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int TrySend(int handle, int type, byte[] payload, int length);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int TryReceive(int handle, int typeFilter, byte[] payload);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int PciFindDevice(int vendorId, int deviceId);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int PciDeviceCount();

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int PciGetDeviceInfo(int index, byte[] buffer);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int PciReadConfig32(int deviceHandle, int registerOffset);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int PciReadBar32(int deviceHandle, int barIndex);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int PciOpenBar(int deviceHandle, int barIndex);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int MmioRead32(long baseAddress, int offset);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int MmioWrite32(long baseAddress, int offset, int value);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int MmioRegionSize(int regionHandle);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int MmioRegionRead32(int regionHandle, int offset);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int MmioRegionWrite32(int regionHandle, int offset, int value);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int CloseMmioRegion(int regionHandle);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int AllocBuffer(int size);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int FreeBuffer(int handle);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int BufferSize(int handle);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int AllocDmaBuffer(int size);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int FreeDmaBuffer(int handle);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int DmaBufferSize(int handle);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern long DmaBufferPhysicalAddress(int handle);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int DmaBufferRead32(int handle, int offset);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int DmaBufferWrite32(int handle, int offset, int value);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int SubscribeIrq(int irqNumber, int channelHandle);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int UnsubscribeIrq(int subscriptionHandle);
}
