namespace Zapada.Drivers;

public static class DriverServiceKind
{
    public const string StorageCore = "storage.core";
    public const string RamFs = "filesystem.ramfs";
    public const string BlockDevice = "block.device";
    public const string PartitionTable = "partition.table";
    public const string FileSystem = "filesystem";
    public const string FileSystemProbe = "filesystem";
    public const string Namespace = "namespace";
    public const string Shell = "shell";
    public const string Diagnostics = "diagnostics";
    public const string DeviceNode = "device.node";
    public const string CharacterDevice = "char.device";
    public const string CharDevice = "char.device";
    public const string NetworkInterface = "network.interface";
    public const string UsbBus = "usb.bus";
    public const string EntropySource = "entropy.source";
    public const string HalSmoke = "hal.smoke";
}
