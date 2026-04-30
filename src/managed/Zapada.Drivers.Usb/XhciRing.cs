using Zapada.Drivers.Hal;
using Zapada.Storage;

namespace Zapada.Drivers.Usb;

internal sealed class XhciRing
{
    private const int TrbBytes = 16;
    private readonly int _trbCount;
    private readonly DmaBuffer _buffer;
    private int _enqueueIndex;
    private int _cycle;

    public XhciRing(int trbCount)
    {
        _trbCount = trbCount;
        _buffer = DmaBuffer.Allocate(trbCount * TrbBytes);
        _enqueueIndex = 0;
        _cycle = 1;
        if (_buffer != null && trbCount > 1)
            WriteLinkTrb();
    }

    public bool IsValid()
    {
        return _buffer != null;
    }

    public DmaBuffer Buffer
    {
        get { return _buffer; }
    }

    public long DequeuePointer
    {
        get { return _buffer.PhysicalAddress | 1L; }
    }

    public long EnqueuePhysicalAddress
    {
        get { return _buffer.PhysicalAddress + (long)_enqueueIndex * TrbBytes; }
    }

    public int Enqueue(long parameter, int status, int control)
    {
        if (_buffer == null)
            return StorageStatus.NoMemory;

        if (_enqueueIndex >= _trbCount - 1)
        {
            WriteLinkTrb();
            _enqueueIndex = 0;
            _cycle = _cycle == 0 ? 1 : 0;
        }

        int offset = _enqueueIndex * TrbBytes;
        _buffer.Write32(offset + 0, UsbBitOps.Low32(parameter));
        _buffer.Write32(offset + 4, UsbBitOps.High32(parameter));
        _buffer.Write32(offset + 8, status);
        _buffer.Write32(offset + 12, control | _cycle);
        _enqueueIndex++;
        return StorageStatus.Ok;
    }

    public int Free()
    {
        if (_buffer == null)
            return StorageStatus.Ok;

        return _buffer.Free();
    }

    private void WriteLinkTrb()
    {
        int offset = (_trbCount - 1) * TrbBytes;
        _buffer.Write32(offset + 0, UsbBitOps.Low32(_buffer.PhysicalAddress));
        _buffer.Write32(offset + 4, UsbBitOps.High32(_buffer.PhysicalAddress));
        _buffer.Write32(offset + 8, 0);
        _buffer.Write32(offset + 12, (XhciConstants.TrbLink << 10) | XhciConstants.TrbControlToggleCycle | _cycle);
    }
}
