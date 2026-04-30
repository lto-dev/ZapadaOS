using Zapada.Drivers.Hal;

namespace Zapada.Drivers;

internal sealed class VirtioPciRegions
{
    public PciDeviceInfo Device;
    public VirtioPciCapability CommonCapability;
    public VirtioPciCapability NotifyCapability;
    public VirtioPciCapability DeviceCapability;
    public MmioRegion CommonRegion;
    public MmioRegion NotifyRegion;
    public MmioRegion DeviceRegion;

    public int Open(PciDeviceInfo device)
    {
        if (device == null)
            return 0;

        VirtioPciCapability common = VirtioPciCapability.Find(device, VirtioConstants.PciCfgTypeCommon);
        VirtioPciCapability notify = VirtioPciCapability.Find(device, VirtioConstants.PciCfgTypeNotify);
        VirtioPciCapability deviceConfig = VirtioPciCapability.Find(device, VirtioConstants.PciCfgTypeDevice);
        if (common == null || notify == null || deviceConfig == null)
            return 0;

        if (common.BarIndex < 0 || common.BarIndex > 5 || notify.BarIndex < 0 || notify.BarIndex > 5 || deviceConfig.BarIndex < 0 || deviceConfig.BarIndex > 5)
            return 0;

        MmioRegion commonRegion = PciBus.OpenBar(device.Handle, common.BarIndex);
        if (commonRegion == null)
            return 0;

        MmioRegion notifyRegion;
        if (notify.BarIndex == common.BarIndex)
            notifyRegion = commonRegion;
        else
        {
            notifyRegion = PciBus.OpenBar(device.Handle, notify.BarIndex);
            if (notifyRegion == null)
            {
                commonRegion.Close();
                return 0;
            }
        }

        MmioRegion deviceRegion;
        if (deviceConfig.BarIndex == common.BarIndex)
            deviceRegion = commonRegion;
        else if (deviceConfig.BarIndex == notify.BarIndex)
            deviceRegion = notifyRegion;
        else
        {
            deviceRegion = PciBus.OpenBar(device.Handle, deviceConfig.BarIndex);
            if (deviceRegion == null)
            {
                if (notifyRegion != commonRegion)
                    notifyRegion.Close();
                commonRegion.Close();
                return 0;
            }
        }

        if (!ValidateRange(commonRegion, common.Offset, 0x40) || !ValidateRange(notifyRegion, notify.Offset, 2) || !ValidateRange(deviceRegion, deviceConfig.Offset, 8))
        {
            CloseDistinct(commonRegion, notifyRegion, deviceRegion);
            return 0;
        }

        PciBus.EnableBusMaster(device.Handle);

        Device = device;
        CommonCapability = common;
        NotifyCapability = notify;
        DeviceCapability = deviceConfig;
        CommonRegion = commonRegion;
        NotifyRegion = notifyRegion;
        DeviceRegion = deviceRegion;
        return 1;
    }

    public void Close()
    {
        CloseDistinct(CommonRegion, NotifyRegion, DeviceRegion);
        CommonRegion = null;
        NotifyRegion = null;
        DeviceRegion = null;
    }

    private static bool ValidateRange(MmioRegion region, int offset, int length)
    {
        if (region == null || offset < 0 || length <= 0)
            return false;

        return offset <= region.Size && region.Size - offset >= length;
    }

    private static void CloseDistinct(MmioRegion first, MmioRegion second, MmioRegion third)
    {
        if (third != null && third != first && third != second)
            third.Close();
        if (second != null && second != first)
            second.Close();
        if (first != null)
            first.Close();
    }
}
