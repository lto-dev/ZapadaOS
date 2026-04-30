using System;
using Zapada.Drivers.Hal;
using Zapada.Storage;

namespace Zapada.Drivers.Usb;

internal sealed class XhciController
{
    private const int CommandRingTrbs = 64;
    private const int TransferRingTrbs = 128;
    private const int EventRingTrbs = 256;
    private const int ContextBytes = 4096;

    private XhciRegisters _registers;
    private XhciRing _commandRing;
    private XhciRing _controlRing;
    private XhciRing _bulkInRing;
    private XhciRing _bulkOutRing;
    private DmaBuffer _eventRing;
    private DmaBuffer _eventRingSegmentTable;
    private DmaBuffer _deviceContextBaseAddressArray;
    private DmaBuffer _inputContext;
    private DmaBuffer _deviceContext;
    private DmaBuffer _controlBuffer;
    private DmaBuffer _bulkBuffer;
    private int _eventIndex;
    private int _eventCycle;
    private int _contextSize;
    private int _slotId;
    private int _portId;
    private int _portSpeed;
    private int _configurationValue;
    private int _interfaceNumber;
    private int _bulkInEndpointId;
    private int _bulkOutEndpointId;
    private int _bulkInMaxPacket;
    private int _bulkOutMaxPacket;
    private int _tag;

    public long SectorCount { get; private set; }

    public int Probe(PciDeviceInfo device)
    {
        if (device == null)
            return StorageStatus.InvalidArgument;

        Console.Write("[USB] probe stage=pci vendor=");
        WriteHex16(device.VendorId);
        Console.Write(" device=");
        WriteHex16(device.DeviceId);
        Console.Write(" class=");
        WriteHex8(device.ClassCode);
        Console.Write(" subclass=");
        WriteHex8(device.Subclass);
        Console.Write(" prog-if=");
        WriteHex8(device.ProgIf);
        Console.Write(" bar0=");
        WriteHex32(device.Bar0);
        Console.Write("\n");

        MmioRegion region = PciBus.OpenBar(device.Handle, 0);
        if (region == null)
        {
            Console.Write("[USB] probe stage=bar-open failed\n");
            return StorageStatus.NotSupported;
        }

        _registers = new XhciRegisters(region);
        _contextSize = _registers.ContextSize;
        _tag = 1;

        Console.Write("[USB] probe stage=caps mmio-size=");
        Console.Write(region.Size);
        Console.Write(" caplen=");
        Console.Write(_registers.CapLength);
        Console.Write(" db-off=");
        WriteHex32(_registers.DoorbellOffset);
        Console.Write(" rt-off=");
        WriteHex32(_registers.RuntimeOffset);
        Console.Write(" slots=");
        Console.Write(_registers.MaxSlots);
        Console.Write(" ports=");
        Console.Write(_registers.MaxPorts);
        Console.Write(" ctx=");
        Console.Write(_contextSize);
        Console.Write(" pagesize=");
        WriteHex32(_registers.ReadOp32(XhciConstants.OpPageSize));
        Console.Write(" usbcmd=");
        WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbCommand));
        Console.Write(" usbsts=");
        WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbStatus));
        Console.Write("\n");

        int irqLine = PciBus.ReadConfig32(device.Handle, 0x3C) & 0xFF;
        Console.Write("[USB] probe stage=irq line=");
        Console.Write(irqLine);
        Console.Write(" deferred\n");

        PciBus.EnableBusMaster(device.Handle);

        int rc = AllocateControllerBuffers();
        if (rc != StorageStatus.Ok)
        {
            Console.Write("[USB] probe stage=dma failed rc=");
            Console.Write(rc);
            Console.Write("\n");
            return rc;
        }

        Console.Write("[USB] probe stage=dma command=");
        WriteHex64(_commandRing.Buffer.PhysicalAddress);
        Console.Write(" event=");
        WriteHex64(_eventRing.PhysicalAddress);
        Console.Write(" erst=");
        WriteHex64(_eventRingSegmentTable.PhysicalAddress);
        Console.Write(" dcbaa=");
        WriteHex64(_deviceContextBaseAddressArray.PhysicalAddress);
        Console.Write("\n");

        rc = InitializeHostControllerBounded();
        if (rc != StorageStatus.Ok)
            return rc;

        rc = ProbePortsOnly();
        StopHostControllerAfterProbe();
        return rc;
    }

    public int Initialize(PciDeviceInfo device)
    {
        if (device == null)
            return StorageStatus.InvalidArgument;

        MmioRegion region = PciBus.OpenBar(device.Handle, 0);
        if (region == null)
            return StorageStatus.NotSupported;

        _registers = new XhciRegisters(region);
        _contextSize = _registers.ContextSize;
        _tag = 1;

        int irqLine = PciBus.ReadConfig32(device.Handle, 0x3C) & 0xFF;
        if (irqLine > 0 && irqLine < 16)
            PciBus.EnableIrq(irqLine);

        PciBus.EnableBusMaster(device.Handle);

        int rc = AllocateControllerBuffers();
        if (rc != StorageStatus.Ok)
            return rc;

        rc = InitializeHostController();
        if (rc != StorageStatus.Ok)
            return rc;

        rc = EnumerateFirstMassStorageDevice();
        if (rc != StorageStatus.Ok)
            return rc;

        return InitializeScsiCapacity();
    }

    public int ReadSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset)
    {
        if (sectorCount <= 0 || sectorCount > XhciConstants.MaxSectorsPerRequest)
            return StorageStatus.InvalidArgument;
        if (buffer == null || bufferOffset < 0 || bufferOffset + sectorCount * XhciConstants.SectorSize > buffer.Length)
            return StorageStatus.InvalidArgument;

        byte[] cdb = new byte[16];
        cdb[0] = 0x28;
        UsbBitOps.WriteBe32ToArray(cdb, 2, lba);
        UsbBitOps.WriteBe16ToArray(cdb, 7, sectorCount);

        int byteCount = sectorCount * XhciConstants.SectorSize;
        int rc = BulkOnlyTransfer(cdb, 10, true, byteCount);
        if (rc != StorageStatus.Ok)
            return rc;

        rc = _bulkBuffer.CopyTo(0, buffer, bufferOffset, byteCount);
        if (rc != DriverHal.StatusSuccess)
            return StorageStatus.IoError;

        return sectorCount;
    }

    public int WriteSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset)
    {
        if (sectorCount <= 0 || sectorCount > XhciConstants.MaxSectorsPerRequest)
            return StorageStatus.InvalidArgument;
        if (buffer == null || bufferOffset < 0 || bufferOffset + sectorCount * XhciConstants.SectorSize > buffer.Length)
            return StorageStatus.InvalidArgument;

        int byteCount = sectorCount * XhciConstants.SectorSize;
        int copyRc = _bulkBuffer.CopyFrom(0, buffer, bufferOffset, byteCount);
        if (copyRc != DriverHal.StatusSuccess)
            return StorageStatus.IoError;

        byte[] cdb = new byte[16];
        cdb[0] = 0x2A;
        UsbBitOps.WriteBe32ToArray(cdb, 2, lba);
        UsbBitOps.WriteBe16ToArray(cdb, 7, sectorCount);
        int rc = BulkOnlyTransfer(cdb, 10, false, byteCount);
        if (rc != StorageStatus.Ok)
            return rc;

        return sectorCount;
    }

    private int AllocateControllerBuffers()
    {
        _commandRing = new XhciRing(CommandRingTrbs);
        _controlRing = new XhciRing(TransferRingTrbs);
        _bulkInRing = new XhciRing(TransferRingTrbs);
        _bulkOutRing = new XhciRing(TransferRingTrbs);
        _eventRing = DmaBuffer.Allocate(EventRingTrbs * 16);
        _eventRingSegmentTable = DmaBuffer.Allocate(4096);
        _deviceContextBaseAddressArray = DmaBuffer.Allocate(4096);
        _inputContext = DmaBuffer.Allocate(ContextBytes);
        _deviceContext = DmaBuffer.Allocate(ContextBytes);
        _controlBuffer = DmaBuffer.Allocate(4096);
        _bulkBuffer = DmaBuffer.Allocate(XhciConstants.MaxSectorsPerRequest * XhciConstants.SectorSize);

        if (_commandRing == null || !_commandRing.IsValid() || _controlRing == null || !_controlRing.IsValid()
            || _bulkInRing == null || !_bulkInRing.IsValid() || _bulkOutRing == null || !_bulkOutRing.IsValid()
            || _eventRing == null || _eventRingSegmentTable == null || _deviceContextBaseAddressArray == null
            || _inputContext == null || _deviceContext == null || _controlBuffer == null || _bulkBuffer == null)
            return StorageStatus.NoMemory;

        _eventIndex = 0;
        _eventCycle = 1;
        return StorageStatus.Ok;
    }

    private int InitializeHostController()
    {
        int command = _registers.ReadOp32(XhciConstants.OpUsbCommand);
        _registers.WriteOp32(XhciConstants.OpUsbCommand, command & ~XhciConstants.UsbCommandRun);
        if (!WaitOpSet(XhciConstants.OpUsbStatus, XhciConstants.UsbStatusHalted, XhciConstants.ProbeAttemptsLong))
            return StorageStatus.BusyResource;

        _registers.WriteOp32(XhciConstants.OpUsbCommand, XhciConstants.UsbCommandHostControllerReset);
        if (!WaitOpClear(XhciConstants.OpUsbCommand, XhciConstants.UsbCommandHostControllerReset, XhciConstants.ProbeAttemptsLong))
            return StorageStatus.BusyResource;
        if (!WaitOpClear(XhciConstants.OpUsbStatus, XhciConstants.UsbStatusControllerNotReady, XhciConstants.ProbeAttemptsLong))
            return StorageStatus.BusyResource;

        int maxSlots = _registers.MaxSlots;
        if (maxSlots <= 0)
            maxSlots = 1;

        ClearDma(_deviceContextBaseAddressArray, 4096);
        ClearDma(_eventRing, EventRingTrbs * 16);
        ClearDma(_eventRingSegmentTable, 4096);
        UsbBitOps.Write64(_eventRingSegmentTable, 0, _eventRing.PhysicalAddress);
        _eventRingSegmentTable.Write32(8, EventRingTrbs);
        _eventRingSegmentTable.Write32(12, 0);

        _registers.WriteOp64(XhciConstants.OpDeviceContextBaseAddressArray, _deviceContextBaseAddressArray.PhysicalAddress);
        _registers.WriteOp64(XhciConstants.OpCommandRingControl, _commandRing.DequeuePointer);
        _registers.WriteOp32(XhciConstants.OpConfigure, maxSlots & 0xFF);

        _registers.WriteRuntime32(0x28, 1);
        _registers.WriteRuntime64(0x30, _eventRingSegmentTable.PhysicalAddress);
        _registers.WriteRuntime64(0x38, _eventRing.PhysicalAddress);
        _registers.WriteRuntime32(0x24, 0);
        _registers.WriteRuntime32(0x20, 2);

        _registers.WriteOp32(XhciConstants.OpUsbStatus, 0x1F);
        _registers.WriteOp32(XhciConstants.OpUsbCommand, XhciConstants.UsbCommandRun | XhciConstants.UsbCommandInterruptEnable);
        if (!WaitOpClear(XhciConstants.OpUsbStatus, XhciConstants.UsbStatusHalted, XhciConstants.ProbeAttemptsLong))
            return StorageStatus.BusyResource;

        return StorageStatus.Ok;
    }

    private int InitializeHostControllerBounded()
    {
        Console.Write("[USB] probe stage=halt begin usbcmd=");
        WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbCommand));
        Console.Write(" usbsts=");
        WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbStatus));
        Console.Write("\n");

        int command = _registers.ReadOp32(XhciConstants.OpUsbCommand);
        _registers.WriteOp32(XhciConstants.OpUsbCommand, command & ~XhciConstants.UsbCommandRun);
        if (!WaitOpSetLogged("halt", XhciConstants.OpUsbStatus, XhciConstants.UsbStatusHalted, XhciConstants.ProbeAttemptsMedium))
            return StorageStatus.BusyResource;

        Console.Write("[USB] probe stage=reset begin usbsts=");
        WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbStatus));
        Console.Write("\n");

        _registers.WriteOp32(XhciConstants.OpUsbCommand, XhciConstants.UsbCommandHostControllerReset);
        if (!WaitOpClearLogged("reset", XhciConstants.OpUsbCommand, XhciConstants.UsbCommandHostControllerReset, XhciConstants.ProbeAttemptsMedium))
            return StorageStatus.BusyResource;
        if (!WaitOpClearLogged("ready", XhciConstants.OpUsbStatus, XhciConstants.UsbStatusControllerNotReady, XhciConstants.ProbeAttemptsMedium))
            return StorageStatus.BusyResource;

        int maxSlots = _registers.MaxSlots;
        if (maxSlots <= 0)
            maxSlots = 1;

        ClearDma(_deviceContextBaseAddressArray, 4096);
        ClearDma(_eventRing, EventRingTrbs * 16);
        ClearDma(_eventRingSegmentTable, 4096);
        UsbBitOps.Write64(_eventRingSegmentTable, 0, _eventRing.PhysicalAddress);
        _eventRingSegmentTable.Write32(8, EventRingTrbs);
        _eventRingSegmentTable.Write32(12, 0);

        _registers.WriteOp64(XhciConstants.OpDeviceContextBaseAddressArray, _deviceContextBaseAddressArray.PhysicalAddress);
        _registers.WriteOp64(XhciConstants.OpCommandRingControl, _commandRing.DequeuePointer);
        _registers.WriteOp32(XhciConstants.OpConfigure, maxSlots & 0xFF);

        int interrupter = XhciConstants.RuntimeInterrupter0;
        _registers.WriteRuntime32(interrupter + XhciConstants.EventRingSegmentTableSize, 1);
        _registers.WriteRuntime64(interrupter + XhciConstants.EventRingSegmentTableBase, _eventRingSegmentTable.PhysicalAddress);
        _registers.WriteRuntime64(interrupter + XhciConstants.EventRingDequeuePointer, _eventRing.PhysicalAddress);
        _registers.WriteRuntime32(interrupter + XhciConstants.InterrupterModeration, 0);
        _registers.WriteRuntime32(interrupter + XhciConstants.InterrupterManagement, 0);

        Console.Write("[USB] probe stage=run begin usbcmd=");
        WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbCommand));
        Console.Write(" usbsts=");
        WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbStatus));
        Console.Write("\n");

        _registers.WriteOp32(XhciConstants.OpUsbStatus, 0x1F);
        _registers.WriteOp32(XhciConstants.OpUsbCommand, XhciConstants.UsbCommandRun);
        if (!WaitOpClearLogged("run", XhciConstants.OpUsbStatus, XhciConstants.UsbStatusHalted, XhciConstants.ProbeAttemptsMedium))
            return StorageStatus.BusyResource;

        Console.Write("[USB] probe stage=run ok usbcmd=");
        WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbCommand));
        Console.Write(" usbsts=");
        WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbStatus));
        Console.Write("\n");
        return StorageStatus.Ok;
    }

    private void StopHostControllerAfterProbe()
    {
        int command = _registers.ReadOp32(XhciConstants.OpUsbCommand);
        _registers.WriteOp32(XhciConstants.OpUsbCommand, command & ~XhciConstants.UsbCommandRun & ~XhciConstants.UsbCommandInterruptEnable);
        if (WaitOpSetLogged("probe-stop", XhciConstants.OpUsbStatus, XhciConstants.UsbStatusHalted, XhciConstants.ProbeAttemptsMedium))
        {
            Console.Write("[USB] probe stage=stop ok usbcmd=");
            WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbCommand));
            Console.Write(" usbsts=");
            WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbStatus));
            Console.Write("\n");
        }
    }

    private int EnumerateFirstMassStorageDevice()
    {
        int port = FindConnectedPort();
        if (port <= 0)
            return StorageStatus.NotFound;

        _portId = port;
        int portStatus = _registers.ReadPortStatus(port);
        _portSpeed = (portStatus >> 10) & 0x0F;

        int rc = EnableSlot();
        if (rc != StorageStatus.Ok)
            return rc;

        rc = AddressDevice();
        if (rc != StorageStatus.Ok)
            return rc;

        byte[] deviceDescriptor = new byte[18];
        rc = GetDescriptor(XhciConstants.DescriptorDevice, 0, deviceDescriptor, 18);
        if (rc != StorageStatus.Ok)
            return rc;

        byte[] configHeader = new byte[9];
        rc = GetDescriptor(XhciConstants.DescriptorConfiguration, 0, configHeader, 9);
        if (rc != StorageStatus.Ok)
            return rc;

        int totalLength = UsbBitOps.ReadLe16(configHeader, 2);
        if (totalLength < 9 || totalLength > 512)
            return StorageStatus.CorruptedData;

        byte[] config = new byte[totalLength];
        rc = GetDescriptor(XhciConstants.DescriptorConfiguration, 0, config, totalLength);
        if (rc != StorageStatus.Ok)
            return rc;

        rc = ParseMassStorageConfiguration(config, totalLength);
        if (rc != StorageStatus.Ok)
            return rc;

        rc = ControlTransfer(0, XhciConstants.RequestSetConfiguration, _configurationValue, 0, 0, null);
        if (rc != StorageStatus.Ok)
            return rc;

        return ConfigureBulkEndpoints();
    }

    private int FindConnectedPort()
    {
        int maxPorts = _registers.MaxPorts;
        for (int port = 1; port <= maxPorts; port++)
        {
            int status = _registers.ReadPortStatus(port);
            if ((status & XhciConstants.PortStatusConnected) == 0)
                continue;

            _registers.WritePortStatus(port, (status & ~XhciConstants.PortStatusChangeMask) | XhciConstants.PortStatusReset);
            for (int attempt = 0; attempt < XhciConstants.ProbeAttemptsLong; attempt++)
            {
                int current = _registers.ReadPortStatus(port);
                if ((current & XhciConstants.PortStatusReset) == 0 && (current & XhciConstants.PortStatusEnabled) != 0)
                    return port;
            }
        }

        return 0;
    }

    private int ProbePortsOnly()
    {
        int maxPorts = _registers.MaxPorts;
        if (maxPorts <= 0)
            return StorageStatus.NotFound;

        int connected = 0;
        for (int port = 1; port <= maxPorts; port++)
        {
            int status = _registers.ReadPortStatus(port);
            Console.Write("[USB] probe port=");
            Console.Write(port);
            Console.Write(" status=");
            WriteHex32(status);
            Console.Write(" connected=");
            Console.Write((status & XhciConstants.PortStatusConnected) != 0 ? "yes" : "no");
            Console.Write(" enabled=");
            Console.Write((status & XhciConstants.PortStatusEnabled) != 0 ? "yes" : "no");
            Console.Write(" speed=");
            Console.Write((status >> 10) & 0x0F);
            Console.Write("\n");
            if ((status & XhciConstants.PortStatusConnected) != 0)
                connected++;
        }

        if (connected <= 0)
        {
            Console.Write("[USB] probe stage=ports no-connected-device\n");
            return StorageStatus.NotFound;
        }

        Console.Write("[USB] probe stage=ports connected=");
        Console.Write(connected);
        Console.Write("\n");
        return StorageStatus.Ok;
    }

    private int EnableSlot()
    {
        long commandPointer = _commandRing.EnqueuePhysicalAddress;
        int rc = _commandRing.Enqueue(0, 0, XhciConstants.TrbEnableSlotCommand << 10);
        if (rc != StorageStatus.Ok)
            return rc;

        _registers.RingDoorbell(0, 0);
        XhciEvent completion = PollEvent(XhciConstants.TrbCommandCompletionEvent, 0, 0, commandPointer, XhciConstants.ProbeAttemptsLong);
        if (completion == null || completion.CompletionCode != XhciConstants.CompletionSuccess || completion.SlotId <= 0)
            return StorageStatus.IoError;

        _slotId = completion.SlotId;
        UsbBitOps.Write64(_deviceContextBaseAddressArray, _slotId * 8, _deviceContext.PhysicalAddress);
        return StorageStatus.Ok;
    }

    private int AddressDevice()
    {
        ClearDma(_inputContext, ContextBytes);
        ClearDma(_deviceContext, ContextBytes);

        _inputContext.Write32(0, 0);
        _inputContext.Write32(4, 0x03);

        int slotOffset = InputOffset(0);
        int ep0Offset = InputOffset(1);
        int maxPacket = DefaultControlMaxPacketSize();
        _inputContext.Write32(slotOffset, (_portSpeed << 20) | (1 << 27));
        _inputContext.Write32(slotOffset + 4, _portId << 16);
        _inputContext.Write32(ep0Offset + 4, (3 << 1) | (XhciConstants.EndpointTypeControl << 3) | (maxPacket << 16));
        UsbBitOps.Write64(_inputContext, ep0Offset + 8, _controlRing.DequeuePointer);
        _inputContext.Write32(ep0Offset + 16, 8);

        long commandPointer = _commandRing.EnqueuePhysicalAddress;
        int rc = _commandRing.Enqueue(_inputContext.PhysicalAddress, 0, (XhciConstants.TrbAddressDeviceCommand << 10) | (_slotId << 24));
        if (rc != StorageStatus.Ok)
            return rc;

        _registers.RingDoorbell(0, 0);
        XhciEvent completion = PollEvent(XhciConstants.TrbCommandCompletionEvent, 0, _slotId, commandPointer, XhciConstants.ProbeAttemptsLong);
        if (completion == null || completion.CompletionCode != XhciConstants.CompletionSuccess)
            return StorageStatus.IoError;

        return StorageStatus.Ok;
    }

    private int GetDescriptor(int descriptorType, int descriptorIndex, byte[] buffer, int length)
    {
        int rc = ControlTransfer(0x80, XhciConstants.RequestGetDescriptor, (descriptorType << 8) | descriptorIndex, 0, length, buffer);
        return rc;
    }

    private int ControlTransfer(int requestType, int request, int value, int index, int length, byte[] buffer)
    {
        if (length > _controlBuffer.Size)
            return StorageStatus.InvalidArgument;

        if (length > 0 && (requestType & 0x80) == 0 && buffer != null)
            _controlBuffer.CopyFrom(0, buffer, 0, length);

        long setup = ((long)requestType & 0xFFL)
            | (((long)request & 0xFFL) << 8)
            | (((long)value & 0xFFFFL) << 16)
            | (((long)index & 0xFFFFL) << 32)
            | (((long)length & 0xFFFFL) << 48);

        int transferType = length == 0 ? XhciConstants.TrbSetupTransferNoData : (((requestType & 0x80) != 0) ? XhciConstants.TrbSetupTransferIn : XhciConstants.TrbSetupTransferOut);
        int rc = _controlRing.Enqueue(setup, 8, (XhciConstants.TrbSetupStage << 10) | XhciConstants.TrbControlIdt | transferType);
        if (rc != StorageStatus.Ok)
            return rc;

        if (length > 0)
        {
            int dataDirection = (requestType & 0x80) != 0 ? XhciConstants.TrbControlDirIn : 0;
            rc = _controlRing.Enqueue(_controlBuffer.PhysicalAddress, length, (XhciConstants.TrbDataStage << 10) | dataDirection);
            if (rc != StorageStatus.Ok)
                return rc;
        }

        int statusDirection = (length > 0 && (requestType & 0x80) != 0) ? 0 : XhciConstants.TrbControlDirIn;
        rc = _controlRing.Enqueue(0, 0, (XhciConstants.TrbStatusStage << 10) | statusDirection | XhciConstants.TrbControlIoc);
        if (rc != StorageStatus.Ok)
            return rc;

        _registers.RingDoorbell(_slotId, XhciConstants.EndpointIdControl);
        XhciEvent completion = PollEvent(XhciConstants.TrbTransferEvent, XhciConstants.EndpointIdControl, _slotId, 0, XhciConstants.ProbeAttemptsLong);
        if (completion == null || !IsSuccessfulTransferCompletion(completion.CompletionCode))
            return StorageStatus.IoError;

        if (length > 0 && (requestType & 0x80) != 0 && buffer != null)
        {
            int copyRc = _controlBuffer.CopyTo(0, buffer, 0, length);
            if (copyRc != DriverHal.StatusSuccess)
                return StorageStatus.IoError;
        }

        return StorageStatus.Ok;
    }

    private int ParseMassStorageConfiguration(byte[] config, int totalLength)
    {
        _configurationValue = config[5] & 0xFF;
        _interfaceNumber = 0;
        _bulkInEndpointId = 0;
        _bulkOutEndpointId = 0;
        _bulkInMaxPacket = 0;
        _bulkOutMaxPacket = 0;

        bool inMassStorageInterface = false;
        int offset = 0;
        while (offset + 2 <= totalLength)
        {
            int length = config[offset] & 0xFF;
            int descriptorType = config[offset + 1] & 0xFF;
            if (length <= 0 || offset + length > totalLength)
                break;

            if (descriptorType == XhciConstants.DescriptorInterface && length >= 9)
            {
                int cls = config[offset + 5] & 0xFF;
                int sub = config[offset + 6] & 0xFF;
                int proto = config[offset + 7] & 0xFF;
                inMassStorageInterface = cls == XhciConstants.UsbClassMassStorage
                    && sub == XhciConstants.UsbSubclassScsiTransparent
                    && proto == XhciConstants.UsbProtocolBulkOnly;
                if (inMassStorageInterface)
                    _interfaceNumber = config[offset + 2] & 0xFF;
            }
            else if (inMassStorageInterface && descriptorType == XhciConstants.DescriptorEndpoint && length >= 7)
            {
                int endpointAddress = config[offset + 2] & 0xFF;
                int attributes = config[offset + 3] & 0xFF;
                int maxPacket = UsbBitOps.ReadLe16(config, offset + 4) & 0x7FF;
                if ((attributes & 3) == 2)
                {
                    int endpointNumber = endpointAddress & 0x0F;
                    if ((endpointAddress & 0x80) != 0)
                    {
                        _bulkInEndpointId = endpointNumber * 2 + 1;
                        _bulkInMaxPacket = maxPacket;
                    }
                    else
                    {
                        _bulkOutEndpointId = endpointNumber * 2;
                        _bulkOutMaxPacket = maxPacket;
                    }
                }
            }

            offset += length;
        }

        if (_configurationValue <= 0 || _bulkInEndpointId <= 0 || _bulkOutEndpointId <= 0)
            return StorageStatus.NotSupported;

        if (_bulkInMaxPacket <= 0)
            _bulkInMaxPacket = 512;
        if (_bulkOutMaxPacket <= 0)
            _bulkOutMaxPacket = 512;

        return StorageStatus.Ok;
    }

    private int ConfigureBulkEndpoints()
    {
        ClearDma(_inputContext, ContextBytes);
        int addFlags = 1 | (1 << _bulkInEndpointId) | (1 << _bulkOutEndpointId);
        _inputContext.Write32(0, 0);
        _inputContext.Write32(4, addFlags);

        CopyOutputContextToInputSlot();
        int maxEndpointId = _bulkInEndpointId > _bulkOutEndpointId ? _bulkInEndpointId : _bulkOutEndpointId;
        int slotOffset = InputOffset(0);
        int slotDw0 = _inputContext.Read32(slotOffset);
        slotDw0 &= unchecked((int)0x07FFFFFF);
        slotDw0 |= maxEndpointId << 27;
        _inputContext.Write32(slotOffset, slotDw0);

        WriteEndpointContext(_bulkInEndpointId, XhciConstants.EndpointTypeBulkIn, _bulkInMaxPacket, _bulkInRing.DequeuePointer);
        WriteEndpointContext(_bulkOutEndpointId, XhciConstants.EndpointTypeBulkOut, _bulkOutMaxPacket, _bulkOutRing.DequeuePointer);

        long commandPointer = _commandRing.EnqueuePhysicalAddress;
        int rc = _commandRing.Enqueue(_inputContext.PhysicalAddress, 0, (XhciConstants.TrbConfigureEndpointCommand << 10) | (_slotId << 24));
        if (rc != StorageStatus.Ok)
            return rc;

        _registers.RingDoorbell(0, 0);
        XhciEvent completion = PollEvent(XhciConstants.TrbCommandCompletionEvent, 0, _slotId, commandPointer, XhciConstants.ProbeAttemptsLong);
        if (completion == null || completion.CompletionCode != XhciConstants.CompletionSuccess)
            return StorageStatus.IoError;

        return StorageStatus.Ok;
    }

    private int InitializeScsiCapacity()
    {
        byte[] inquiry = new byte[16];
        inquiry[0] = 0x12;
        inquiry[4] = 36;
        int rc = BulkOnlyTransfer(inquiry, 6, true, 36);
        if (rc != StorageStatus.Ok)
            return rc;

        byte[] readCapacity = new byte[16];
        readCapacity[0] = 0x25;
        rc = BulkOnlyTransfer(readCapacity, 10, true, 8);
        if (rc != StorageStatus.Ok)
            return rc;

        byte[] capacity = new byte[8];
        int copyRc = _bulkBuffer.CopyTo(0, capacity, 0, 8);
        if (copyRc != DriverHal.StatusSuccess)
            return StorageStatus.IoError;

        long lastLba = UsbBitOps.ReadBe32(capacity, 0);
        long blockSize = UsbBitOps.ReadBe32(capacity, 4);
        if (lastLba < 0 || blockSize != XhciConstants.SectorSize)
            return StorageStatus.NotSupported;

        SectorCount = lastLba + 1;
        return SectorCount > 0 ? StorageStatus.Ok : StorageStatus.CorruptedData;
    }

    private int BulkOnlyTransfer(byte[] cdb, int cdbLength, bool dataIn, int transferLength)
    {
        byte[] cbw = new byte[31];
        int tag = _tag++;
        cbw[0] = 0x55;
        cbw[1] = 0x53;
        cbw[2] = 0x42;
        cbw[3] = 0x43;
        WriteLe32ToArray(cbw, 4, tag);
        WriteLe32ToArray(cbw, 8, transferLength);
        cbw[12] = dataIn ? (byte)0x80 : (byte)0x00;
        cbw[13] = 0;
        cbw[14] = (byte)cdbLength;
        for (int i = 0; i < cdbLength && i < 16; i++)
            cbw[15 + i] = cdb[i];

        int rc = _controlBuffer.CopyFrom(0, cbw, 0, cbw.Length);
        if (rc != DriverHal.StatusSuccess)
            return StorageStatus.IoError;
        rc = BulkTransfer(false, _controlBuffer.PhysicalAddress, cbw.Length);
        if (rc != StorageStatus.Ok)
            return rc;

        if (transferLength > 0)
        {
            rc = BulkTransfer(dataIn, _bulkBuffer.PhysicalAddress, transferLength);
            if (rc != StorageStatus.Ok)
                return rc;
        }

        rc = BulkTransfer(true, _controlBuffer.PhysicalAddress, 13);
        if (rc != StorageStatus.Ok)
            return rc;

        byte[] csw = new byte[13];
        rc = _controlBuffer.CopyTo(0, csw, 0, 13);
        if (rc != DriverHal.StatusSuccess)
            return StorageStatus.IoError;

        if (csw[0] != 0x55 || csw[1] != 0x53 || csw[2] != 0x42 || csw[3] != 0x53)
            return StorageStatus.IoError;
        if (UsbBitOps.ReadLe32(csw, 4) != tag)
            return StorageStatus.IoError;
        if ((csw[12] & 0xFF) != 0)
            return StorageStatus.IoError;

        return StorageStatus.Ok;
    }

    private int BulkTransfer(bool inDirection, long bufferPhysicalAddress, int length)
    {
        XhciRing ring = inDirection ? _bulkInRing : _bulkOutRing;
        int endpointId = inDirection ? _bulkInEndpointId : _bulkOutEndpointId;
        int rc = ring.Enqueue(bufferPhysicalAddress, length, (XhciConstants.TrbNormal << 10) | XhciConstants.TrbControlIoc);
        if (rc != StorageStatus.Ok)
            return rc;

        _registers.RingDoorbell(_slotId, endpointId);
        XhciEvent completion = PollEvent(XhciConstants.TrbTransferEvent, endpointId, _slotId, 0, XhciConstants.ProbeAttemptsLong);
        if (completion == null || !IsSuccessfulTransferCompletion(completion.CompletionCode))
            return StorageStatus.IoError;

        return StorageStatus.Ok;
    }

    private XhciEvent PollEvent(int expectedType, int endpointId, int slotId, long commandPointer, int attempts)
    {
        for (int attempt = 0; attempt < attempts; attempt++)
        {
            int offset = _eventIndex * 16;
            int control = _eventRing.Read32(offset + 12);
            if ((control & 1) != _eventCycle)
                continue;

            XhciEvent evt = new XhciEvent();
            int p0 = _eventRing.Read32(offset + 0);
            int p1 = _eventRing.Read32(offset + 4);
            evt.Parameter = ((long)p0 & 0xFFFFFFFFL) | (((long)p1 & 0xFFFFFFFFL) << 32);
            evt.Status = _eventRing.Read32(offset + 8);
            evt.Control = control;
            evt.Type = (control >> 10) & 0x3F;
            evt.EndpointId = (control >> 16) & 0x1F;
            evt.SlotId = (control >> 24) & 0xFF;
            evt.CompletionCode = (evt.Status >> 24) & 0xFF;
            AdvanceEventRing();

            bool matches = evt.Type == expectedType;
            if (matches && endpointId > 0)
                matches = evt.EndpointId == endpointId;
            if (matches && slotId > 0)
                matches = evt.SlotId == slotId;
            if (matches && commandPointer != 0)
                matches = evt.Parameter == commandPointer;

            if (matches)
                return evt;
        }

        return null;
    }

    private void AdvanceEventRing()
    {
        _eventIndex++;
        if (_eventIndex >= EventRingTrbs)
        {
            _eventIndex = 0;
            _eventCycle = _eventCycle == 0 ? 1 : 0;
        }

        long dequeue = _eventRing.PhysicalAddress + (long)_eventIndex * 16;
        _registers.WriteRuntime64(0x38, dequeue | 8L);
    }

    private void WriteEndpointContext(int endpointId, int endpointType, int maxPacket, long ringPointer)
    {
        int offset = InputOffset(endpointId);
        _inputContext.Write32(offset + 0, 0);
        _inputContext.Write32(offset + 4, (3 << 1) | (endpointType << 3) | (maxPacket << 16));
        UsbBitOps.Write64(_inputContext, offset + 8, ringPointer);
        _inputContext.Write32(offset + 16, maxPacket);
    }

    private void CopyOutputContextToInputSlot()
    {
        int inputSlot = InputOffset(0);
        for (int i = 0; i < _contextSize; i += 4)
            _inputContext.Write32(inputSlot + i, _deviceContext.Read32(i));
    }

    private int InputOffset(int deviceContextIndex)
    {
        return (deviceContextIndex + 1) * _contextSize;
    }

    private int DefaultControlMaxPacketSize()
    {
        if (_portSpeed >= 4)
            return 512;

        return 64;
    }

    private static bool IsSuccessfulTransferCompletion(int completionCode)
    {
        return completionCode == XhciConstants.CompletionSuccess || completionCode == XhciConstants.CompletionShortPacket;
    }

    private bool WaitOpSet(int offset, int mask, int attempts)
    {
        for (int i = 0; i < attempts; i++)
        {
            if ((_registers.ReadOp32(offset) & mask) == mask)
                return true;
        }

        return false;
    }

    private bool WaitOpClear(int offset, int mask, int attempts)
    {
        for (int i = 0; i < attempts; i++)
        {
            if ((_registers.ReadOp32(offset) & mask) == 0)
                return true;
        }

        return false;
    }

    private bool WaitOpSetLogged(string stage, int offset, int mask, int attempts)
    {
        bool ok = WaitOpSet(offset, mask, attempts);
        if (!ok)
            PrintWaitTimeout(stage, offset, mask, true);
        return ok;
    }

    private bool WaitOpClearLogged(string stage, int offset, int mask, int attempts)
    {
        bool ok = WaitOpClear(offset, mask, attempts);
        if (!ok)
            PrintWaitTimeout(stage, offset, mask, false);
        return ok;
    }

    private void PrintWaitTimeout(string stage, int offset, int mask, bool waitingForSet)
    {
        Console.Write("[USB] probe stage=");
        Console.Write(stage);
        Console.Write(" wait-timeout offset=");
        WriteHex32(offset);
        Console.Write(" mask=");
        WriteHex32(mask);
        Console.Write(" want=");
        Console.Write(waitingForSet ? "set" : "clear");
        Console.Write(" value=");
        WriteHex32(_registers.ReadOp32(offset));
        Console.Write(" usbcmd=");
        WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbCommand));
        Console.Write(" usbsts=");
        WriteHex32(_registers.ReadOp32(XhciConstants.OpUsbStatus));
        Console.Write("\n");
    }

    private static void ClearDma(DmaBuffer buffer, int bytes)
    {
        if (buffer == null)
            return;

        for (int i = 0; i < bytes; i += 4)
            buffer.Write32(i, 0);
    }

    private static void WriteLe32ToArray(byte[] buffer, int offset, int value)
    {
        buffer[offset] = (byte)(value & 0xFF);
        buffer[offset + 1] = (byte)((value >> 8) & 0xFF);
        buffer[offset + 2] = (byte)((value >> 16) & 0xFF);
        buffer[offset + 3] = (byte)((value >> 24) & 0xFF);
    }

    private static void WriteHex8(int value)
    {
        Console.Write("0x");
        WriteHexDigit((value >> 4) & 0x0F);
        WriteHexDigit(value & 0x0F);
    }

    private static void WriteHex16(int value)
    {
        Console.Write("0x");
        for (int shift = 12; shift >= 0; shift -= 4)
            WriteHexDigit((value >> shift) & 0x0F);
    }

    private static void WriteHex32(int value)
    {
        Console.Write("0x");
        for (int shift = 28; shift >= 0; shift -= 4)
            WriteHexDigit((value >> shift) & 0x0F);
    }

    private static void WriteHex64(long value)
    {
        Console.Write("0x");
        for (int shift = 60; shift >= 0; shift -= 4)
            WriteHexDigit((int)((value >> shift) & 0x0FL));
    }

    private static void WriteHexDigit(int value)
    {
        if (value < 10)
        {
            Console.Write((char)('0' + value));
            return;
        }

        Console.Write((char)('a' + (value - 10)));
    }
}
