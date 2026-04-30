namespace Zapada.Drivers.Usb;

internal static class XhciConstants
{
    public const int PciClassSerialBus = 0x0C;
    public const int PciSubclassUsb = 0x03;
    public const int PciProgIfXhci = 0x30;

    public const int UsbClassMassStorage = 0x08;
    public const int UsbSubclassScsiTransparent = 0x06;
    public const int UsbProtocolBulkOnly = 0x50;

    public const int TrbNormal = 1;
    public const int TrbSetupStage = 2;
    public const int TrbDataStage = 3;
    public const int TrbStatusStage = 4;
    public const int TrbLink = 6;
    public const int TrbEnableSlotCommand = 9;
    public const int TrbAddressDeviceCommand = 11;
    public const int TrbConfigureEndpointCommand = 12;
    public const int TrbTransferEvent = 32;
    public const int TrbCommandCompletionEvent = 33;
    public const int TrbPortStatusChangeEvent = 34;

    public const int TrbControlIoc = 1 << 5;
    public const int TrbControlIdt = 1 << 6;
    public const int TrbControlDirIn = 1 << 16;
    public const int TrbControlToggleCycle = 1 << 1;
    public const int TrbSetupTransferNoData = 0 << 16;
    public const int TrbSetupTransferOut = 2 << 16;
    public const int TrbSetupTransferIn = 3 << 16;

    public const int CompletionSuccess = 1;
    public const int CompletionShortPacket = 13;

    public const int EndpointIdControl = 1;
    public const int EndpointTypeControl = 4;
    public const int EndpointTypeBulkOut = 2;
    public const int EndpointTypeBulkIn = 6;

    public const int OpUsbCommand = 0x00;
    public const int OpUsbStatus = 0x04;
    public const int OpPageSize = 0x08;
    public const int OpCommandRingControl = 0x18;
    public const int OpDeviceContextBaseAddressArray = 0x30;
    public const int OpConfigure = 0x38;
    public const int RuntimeInterrupter0 = 0x20;
    public const int InterrupterManagement = 0x00;
    public const int InterrupterModeration = 0x04;
    public const int EventRingSegmentTableSize = 0x08;
    public const int EventRingSegmentTableBase = 0x10;
    public const int EventRingDequeuePointer = 0x18;

    public const int UsbCommandRun = 1 << 0;
    public const int UsbCommandHostControllerReset = 1 << 1;
    public const int UsbCommandInterruptEnable = 1 << 2;
    public const int UsbStatusHalted = 1 << 0;
    public const int UsbStatusControllerNotReady = 1 << 11;

    public const int PortStatusConnected = 1 << 0;
    public const int PortStatusEnabled = 1 << 1;
    public const int PortStatusReset = 1 << 4;
    public const int PortStatusPower = 1 << 9;
    public const int PortStatusChangeMask = (1 << 17) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21) | (1 << 22) | (1 << 23);

    public const int RequestGetDescriptor = 6;
    public const int RequestSetConfiguration = 9;
    public const int DescriptorDevice = 1;
    public const int DescriptorConfiguration = 2;
    public const int DescriptorInterface = 4;
    public const int DescriptorEndpoint = 5;

    public const int ProbeAttemptsShort = 4096;
    public const int ProbeAttemptsMedium = 32768;
    public const int ProbeAttemptsLong = 131072;

    public const int SectorSize = 512;
    public const int MaxSectorsPerRequest = 8;
}
