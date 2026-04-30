using Zapada.Drivers.Hal;

namespace Zapada.Drivers.Usb;

internal sealed class XhciRegisters
{
    private readonly MmioRegion _region;
    private int _capLength;
    private int _doorbellOffset;
    private int _runtimeOffset;

    public XhciRegisters(MmioRegion region)
    {
        _region = region;
        _capLength = region.Read8(0);
        _doorbellOffset = region.Read32(0x14) & unchecked((int)0xFFFFFFFC);
        _runtimeOffset = region.Read32(0x18) & unchecked((int)0xFFFFFFE0);
    }

    public int MaxSlots
    {
        get { return ReadCap32(0x04) & 0xFF; }
    }

    public int MaxPorts
    {
        get { return (ReadCap32(0x04) >> 24) & 0xFF; }
    }

    public int CapLength
    {
        get { return _capLength; }
    }

    public int DoorbellOffset
    {
        get { return _doorbellOffset; }
    }

    public int RuntimeOffset
    {
        get { return _runtimeOffset; }
    }

    public int ContextSize
    {
        get { return (ReadCap32(0x10) & 0x04) != 0 ? 64 : 32; }
    }

    public int ReadCap32(int offset)
    {
        return _region.Read32(offset);
    }

    public int ReadOp32(int offset)
    {
        return _region.Read32(_capLength + offset);
    }

    public int WriteOp32(int offset, int value)
    {
        return _region.Write32(_capLength + offset, value);
    }

    public void WriteOp64(int offset, long value)
    {
        WriteOp32(offset + 4, UsbBitOps.High32(value));
        WriteOp32(offset, UsbBitOps.Low32(value));
    }

    public int ReadPortStatus(int portNumber)
    {
        return ReadOp32(0x400 + (portNumber - 1) * 0x10);
    }

    public int WritePortStatus(int portNumber, int value)
    {
        return WriteOp32(0x400 + (portNumber - 1) * 0x10, value);
    }

    public int ReadRuntime32(int offset)
    {
        return _region.Read32(_runtimeOffset + offset);
    }

    public int WriteRuntime32(int offset, int value)
    {
        return _region.Write32(_runtimeOffset + offset, value);
    }

    public void WriteRuntime64(int offset, long value)
    {
        WriteRuntime32(offset + 4, UsbBitOps.High32(value));
        WriteRuntime32(offset, UsbBitOps.Low32(value));
    }

    public int RingDoorbell(int slotId, int value)
    {
        return _region.Write32(_doorbellOffset + slotId * 4, value);
    }

    public int Close()
    {
        return _region.Close();
    }
}
