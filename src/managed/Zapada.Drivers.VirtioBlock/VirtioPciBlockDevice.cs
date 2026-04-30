using Zapada.Drivers.Hal;
using Zapada.Storage;

namespace Zapada.Drivers;

internal sealed class VirtioPciBlockDevice : BlockDevice
{
    private readonly BlockDeviceInfo _info = new BlockDeviceInfo();
    private VirtioPciRegions _regions;
    private DmaBuffer _descriptorBuffer;
    private DmaBuffer _availableBuffer;
    private DmaBuffer _usedBuffer;
    private DmaBuffer _headerBuffer;
    private DmaBuffer _statusBuffer;
    private DmaBuffer _dataBuffer;
    private int _queueSize;
    private int _lastUsedIndex;
    private int _notifyOffset;
    private bool _initialized;

    public int Initialize(string name, PciDeviceInfo device)
    {
        if (name == null || name.Length == 0 || device == null)
            return StorageStatus.InvalidArgument;

        VirtioPciRegions regions = new VirtioPciRegions();
        if (regions.Open(device) == 0)
            return StorageStatus.NotSupported;

        _regions = regions;

        int rc = InitializeTransport();
        if (rc != StorageStatus.Ok)
        {
            Cleanup();
            return rc;
        }

        long capacity = ReadDeviceCapacity();
        if (capacity <= 0)
        {
            Cleanup();
            return StorageStatus.CorruptedData;
        }

        _info.Initialize(name, "virtio-blk", VirtioConstants.SectorSize, capacity, false, false);
        _initialized = true;
        return StorageStatus.Ok;
    }

    public override BlockDeviceInfo GetInfo()
    {
        return _info;
    }

    public override int ReadSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset)
    {
        return Transfer(VirtioConstants.BlockRequestIn, lba, sectorCount, buffer, bufferOffset);
    }

    public override int WriteSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset)
    {
        return Transfer(VirtioConstants.BlockRequestOut, lba, sectorCount, buffer, bufferOffset);
    }

    private int InitializeTransport()
    {
        _descriptorBuffer = DmaBuffer.Allocate(4096);
        _availableBuffer = DmaBuffer.Allocate(4096);
        _usedBuffer = DmaBuffer.Allocate(4096);
        _headerBuffer = DmaBuffer.Allocate(4096);
        _statusBuffer = DmaBuffer.Allocate(4096);
        _dataBuffer = DmaBuffer.Allocate(VirtioConstants.MaxSectorsPerRequest * VirtioConstants.SectorSize);
        if (_descriptorBuffer == null || _availableBuffer == null || _usedBuffer == null || _headerBuffer == null || _statusBuffer == null || _dataBuffer == null)
            return StorageStatus.NoMemory;

        WriteCommon8(VirtioConstants.CommonDeviceStatus, 0);
        WriteCommon8(VirtioConstants.CommonDeviceStatus, VirtioConstants.StatusAcknowledge);
        WriteCommon8(VirtioConstants.CommonDeviceStatus, ReadCommon8(VirtioConstants.CommonDeviceStatus) | VirtioConstants.StatusDriver);

        WriteCommon32(VirtioConstants.CommonDriverFeatureSelect, 0);
        WriteCommon32(VirtioConstants.CommonDriverFeature, 0);
        WriteCommon32(VirtioConstants.CommonDriverFeatureSelect, 1);
        WriteCommon32(VirtioConstants.CommonDriverFeature, VirtioConstants.FeatureVersion1Page1);

        WriteCommon8(VirtioConstants.CommonDeviceStatus, ReadCommon8(VirtioConstants.CommonDeviceStatus) | VirtioConstants.StatusFeaturesOk);
        if ((ReadCommon8(VirtioConstants.CommonDeviceStatus) & VirtioConstants.StatusFeaturesOk) == 0)
        {
            WriteCommon8(VirtioConstants.CommonDeviceStatus, VirtioConstants.StatusFailed);
            return StorageStatus.NotSupported;
        }

        WriteCommon16(VirtioConstants.CommonQueueSelect, 0);
        int maxQueueSize = ReadCommon16(VirtioConstants.CommonQueueSize);
        if (maxQueueSize <= 0)
            return StorageStatus.NotSupported;

        _queueSize = maxQueueSize;
        if (_queueSize > VirtioConstants.QueueSize)
            _queueSize = VirtioConstants.QueueSize;

        WriteCommon16(VirtioConstants.CommonQueueSize, _queueSize);
        WriteCommon32(VirtioConstants.CommonQueueDescLo, Low32(_descriptorBuffer.PhysicalAddress));
        WriteCommon32(VirtioConstants.CommonQueueDescHi, High32(_descriptorBuffer.PhysicalAddress));
        WriteCommon32(VirtioConstants.CommonQueueDriverLo, Low32(_availableBuffer.PhysicalAddress));
        WriteCommon32(VirtioConstants.CommonQueueDriverHi, High32(_availableBuffer.PhysicalAddress));
        WriteCommon32(VirtioConstants.CommonQueueDeviceLo, Low32(_usedBuffer.PhysicalAddress));
        WriteCommon32(VirtioConstants.CommonQueueDeviceHi, High32(_usedBuffer.PhysicalAddress));
        _notifyOffset = ReadCommon16(VirtioConstants.CommonQueueNotifyOff);
        WriteCommon16(VirtioConstants.CommonQueueEnable, 1);

        WriteCommon8(VirtioConstants.CommonDeviceStatus, ReadCommon8(VirtioConstants.CommonDeviceStatus) | VirtioConstants.StatusDriverOk);
        return StorageStatus.Ok;
    }

    private int Transfer(int requestType, long lba, int sectorCount, byte[] buffer, int bufferOffset)
    {
        if (!_initialized || buffer == null || sectorCount <= 0 || bufferOffset < 0 || sectorCount > VirtioConstants.MaxSectorsPerRequest)
            return StorageStatus.InvalidArgument;

        int byteCount = sectorCount * VirtioConstants.SectorSize;
        if (bufferOffset + byteCount > buffer.Length)
            return StorageStatus.InvalidArgument;
        if (lba < 0 || (_info.SectorCount > 0 && lba + sectorCount > _info.SectorCount))
            return StorageStatus.InvalidArgument;

        ClearRequestBuffers(byteCount);
        WriteHeader(requestType, lba);
        if (requestType == VirtioConstants.BlockRequestOut)
        {
            int copyRc = _dataBuffer.CopyFrom(0, buffer, bufferOffset, byteCount);
            if (copyRc != DriverHal.StatusSuccess)
                return StorageStatus.IoError;
        }

        int flags = requestType == VirtioConstants.BlockRequestIn ? VirtioConstants.DescriptorFlagWrite | VirtioConstants.DescriptorFlagNext : VirtioConstants.DescriptorFlagNext;
        WriteDescriptor(0, _headerBuffer.PhysicalAddress, VirtioConstants.RequestHeaderBytes, VirtioConstants.DescriptorFlagNext, 1);
        WriteDescriptor(1, _dataBuffer.PhysicalAddress, byteCount, flags, 2);
        WriteDescriptor(2, _statusBuffer.PhysicalAddress, VirtioConstants.RequestStatusBytes, VirtioConstants.DescriptorFlagWrite, 0);

        int expectedUsed = _lastUsedIndex + 1;
        SubmitDescriptor(0);
        int usedId = PollUsed(expectedUsed);
        if (usedId != 0)
            return StorageStatus.IoError;

        if (_statusBuffer.Read8(0) != 0)
            return StorageStatus.IoError;

        if (requestType == VirtioConstants.BlockRequestIn)
        {
            int copyRc = _dataBuffer.CopyTo(0, buffer, bufferOffset, byteCount);
            if (copyRc != DriverHal.StatusSuccess)
                return StorageStatus.IoError;
        }

        return sectorCount;
    }

    private void SubmitDescriptor(int descriptorIndex)
    {
        int slot = _lastUsedIndex & (_queueSize - 1);
        _availableBuffer.Write16(4 + slot * 2, descriptorIndex);
        _availableBuffer.Write16(2, (_lastUsedIndex + 1) & 0xFFFF);
        NotifyQueue();
    }

    private int PollUsed(int expectedUsed)
    {
        for (int i = 0; i < 10000000; i++)
        {
            int usedIndex = _usedBuffer.Read16(2);
            if (((usedIndex - _lastUsedIndex) & 0xFFFF) != 0)
            {
                int ringSlot = _lastUsedIndex & (_queueSize - 1);
                int id = _usedBuffer.Read32(4 + ringSlot * 8);
                _lastUsedIndex = expectedUsed;
                return id;
            }
        }

        return -1;
    }

    private void NotifyQueue()
    {
        int notifyAddress = _regions.NotifyCapability.Offset + _notifyOffset * _regions.NotifyCapability.NotifyOffsetMultiplier;
        _regions.NotifyRegion.Write16(notifyAddress, 0);
    }

    private void WriteHeader(int requestType, long lba)
    {
        _headerBuffer.Write32(0, requestType);
        _headerBuffer.Write32(4, 0);
        _headerBuffer.Write32(8, Low32(lba));
        _headerBuffer.Write32(12, High32(lba));
    }

    private void WriteDescriptor(int index, long address, int length, int flags, int next)
    {
        int offset = index * 16;
        _descriptorBuffer.Write32(offset + 0, Low32(address));
        _descriptorBuffer.Write32(offset + 4, High32(address));
        _descriptorBuffer.Write32(offset + 8, length);
        _descriptorBuffer.Write16(offset + 12, flags);
        _descriptorBuffer.Write16(offset + 14, next);
    }

    private void ClearRequestBuffers(int byteCount)
    {
        for (int i = 0; i < 3 * 16; i += 4)
            _descriptorBuffer.Write32(i, 0);
        for (int i = 0; i < 16; i += 4)
            _headerBuffer.Write32(i, 0);
        for (int i = 0; i < byteCount; i += 4)
            _dataBuffer.Write32(i, 0);
        _statusBuffer.Write8(0, 0xFF);
    }

    private long ReadDeviceCapacity()
    {
        int lo = _regions.DeviceRegion.Read32(_regions.DeviceCapability.Offset + 0);
        int hi = _regions.DeviceRegion.Read32(_regions.DeviceCapability.Offset + 4);
        return ((long)hi << 32) | ((long)lo & 0xFFFFFFFFL);
    }

    private int ReadCommon8(int offset)
    {
        return _regions.CommonRegion.Read8(_regions.CommonCapability.Offset + offset);
    }

    private int ReadCommon16(int offset)
    {
        return _regions.CommonRegion.Read16(_regions.CommonCapability.Offset + offset);
    }

    private void WriteCommon8(int offset, int value)
    {
        _regions.CommonRegion.Write8(_regions.CommonCapability.Offset + offset, value);
    }

    private void WriteCommon16(int offset, int value)
    {
        _regions.CommonRegion.Write16(_regions.CommonCapability.Offset + offset, value);
    }

    private void WriteCommon32(int offset, int value)
    {
        _regions.CommonRegion.Write32(_regions.CommonCapability.Offset + offset, value);
    }

    private static int Low32(long value)
    {
        return (int)(value & 0xFFFFFFFFL);
    }

    private static int High32(long value)
    {
        return (int)((value >> 32) & 0xFFFFFFFFL);
    }

    private void Cleanup()
    {
        if (_dataBuffer != null) _dataBuffer.Free();
        if (_statusBuffer != null) _statusBuffer.Free();
        if (_headerBuffer != null) _headerBuffer.Free();
        if (_usedBuffer != null) _usedBuffer.Free();
        if (_availableBuffer != null) _availableBuffer.Free();
        if (_descriptorBuffer != null) _descriptorBuffer.Free();
        if (_regions != null) _regions.Close();
    }
}
