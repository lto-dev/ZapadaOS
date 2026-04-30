using System;
using Zapada.Drivers;
using Zapada.Drivers.Hal;
using Zapada.Storage;

namespace Zapada.Drivers.Usb;

internal static class UsbStorageManager
{
    public static int RegisterManagedDevices()
    {
        int pciCount = PciBus.DeviceCount();
        for (int i = 0; i < pciCount; i++)
        {
            PciDeviceInfo device = PciBus.GetDevice(i);
            if (device == null || device.ClassCode != XhciConstants.PciClassSerialBus || device.Subclass != XhciConstants.PciSubclassUsb || device.ProgIf != XhciConstants.PciProgIfXhci)
                continue;

            Console.Write("[USB] xHCI controller found; running bounded probe only\n");
            XhciController controller = new XhciController();
            int probeRc = controller.Probe(device);
            Console.Write("[USB] xHCI bounded probe rc=");
            Console.Write(probeRc);
            Console.Write("\n");

            if (probeRc == StorageStatus.Ok)
            {
                DriverRegistry.SetState("xhci", DriverState.Bound);
                DriverRegistry.SetFailed("usb-storage", "enumeration deferred after bounded xHCI probe");
                Console.Write("[Gate] Phase-XhciProbe\n");
                Console.Write("[Gate] Phase-UsbStorageDeferred\n");
            }
            else
            {
                DriverRegistry.SetFailed("xhci", "bounded probe failed");
                DriverRegistry.SetFailed("usb-storage", "deferred after xHCI probe failure");
            }

            return 0;
        }

        return 0;
    }
}
