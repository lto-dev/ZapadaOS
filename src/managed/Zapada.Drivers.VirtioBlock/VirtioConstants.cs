namespace Zapada.Drivers;

internal static class VirtioConstants
{
    public const int VendorId = 0x1AF4;
    public const int BlockLegacyDeviceId = 0x1001;
    public const int BlockModernDeviceId = 0x1042;

    public const int PciCapabilityListPointer = 0x34;
    public const int PciCapabilityIdVendorSpecific = 0x09;
    public const int PciStatusOffset = 0x04;
    public const int PciStatusCapabilitiesList = 0x00100000;

    public const int PciCfgTypeCommon = 1;
    public const int PciCfgTypeNotify = 2;
    public const int PciCfgTypeDevice = 4;

    public const int QueueSize = 64;
    public const int SectorSize = 512;
    public const int QueueDescriptorBytes = QueueSize * 16;
    public const int QueueAvailBytes = 6 + QueueSize * 2;
    public const int QueueUsedBytes = 6 + QueueSize * 8;
    public const int RequestHeaderBytes = 16;
    public const int RequestStatusBytes = 1;
    public const int MaxSectorsPerRequest = 8;

    public const int CommonDeviceFeatureSelect = 0x000;
    public const int CommonDeviceFeature = 0x004;
    public const int CommonDriverFeatureSelect = 0x008;
    public const int CommonDriverFeature = 0x00C;
    public const int CommonConfigMsixVector = 0x010;
    public const int CommonNumQueues = 0x012;
    public const int CommonDeviceStatus = 0x014;
    public const int CommonConfigGeneration = 0x015;
    public const int CommonQueueSelect = 0x016;
    public const int CommonQueueSize = 0x018;
    public const int CommonQueueMsixVector = 0x01A;
    public const int CommonQueueEnable = 0x01C;
    public const int CommonQueueNotifyOff = 0x01E;
    public const int CommonQueueDescLo = 0x020;
    public const int CommonQueueDescHi = 0x024;
    public const int CommonQueueDriverLo = 0x028;
    public const int CommonQueueDriverHi = 0x02C;
    public const int CommonQueueDeviceLo = 0x030;
    public const int CommonQueueDeviceHi = 0x034;

    public const int StatusAcknowledge = 0x01;
    public const int StatusDriver = 0x02;
    public const int StatusDriverOk = 0x04;
    public const int StatusFeaturesOk = 0x08;
    public const int StatusFailed = 0x80;

    public const int FeatureVersion1Page1 = 1;

    public const int DescriptorFlagNext = 0x0001;
    public const int DescriptorFlagWrite = 0x0002;

    public const int BlockRequestIn = 0;
    public const int BlockRequestOut = 1;
}
