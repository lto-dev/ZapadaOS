using Zapada.Drivers.Hal;

namespace Zapada.Drivers;

internal sealed class VirtioPciCapability
{
    public int ConfigType;
    public int BarIndex;
    public int Offset;
    public int Length;
    public int NotifyOffsetMultiplier;

    public static VirtioPciCapability Find(PciDeviceInfo device, int configType)
    {
        if (device == null)
            return null;

        int status = DriverHal.PciReadConfig32(device.Handle, VirtioConstants.PciStatusOffset);
        if ((status & VirtioConstants.PciStatusCapabilitiesList) == 0)
            return null;

        int cap = ReadConfig8(device.Handle, VirtioConstants.PciCapabilityListPointer);
        int guard = 0;
        while (cap >= 0x40 && guard < 48)
        {
            int capabilityId = ReadConfig8(device.Handle, cap + 0);
            int next = ReadConfig8(device.Handle, cap + 1);

            if (capabilityId == VirtioConstants.PciCapabilityIdVendorSpecific)
            {
                int foundType = ReadConfig8(device.Handle, cap + 3);
                if (foundType == configType)
                {
                    VirtioPciCapability capability = new VirtioPciCapability();
                    capability.ConfigType = foundType;
                    capability.BarIndex = ReadConfig8(device.Handle, cap + 4);
                    capability.Offset = DriverHal.PciReadConfig32(device.Handle, cap + 8);
                    capability.Length = DriverHal.PciReadConfig32(device.Handle, cap + 12);
                    if (foundType == VirtioConstants.PciCfgTypeNotify)
                        capability.NotifyOffsetMultiplier = DriverHal.PciReadConfig32(device.Handle, cap + 16);
                    return capability;
                }
            }

            if (next == 0 || next == cap)
                break;

            cap = next;
            guard++;
        }

        return null;
    }

    private static int ReadConfig8(int deviceHandle, int offset)
    {
        int value = DriverHal.PciReadConfig32(deviceHandle, offset & ~3);
        return (value >> ((offset & 3) * 8)) & 0xFF;
    }
}
