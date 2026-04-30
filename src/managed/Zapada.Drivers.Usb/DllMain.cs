using System;
using Zapada.Drivers;

namespace Zapada.Drivers.Usb;

public static class DllMain
{
    public static int Initialize()
    {
        DriverRegistry.Register(
            "xhci",
            "Zapada.Drivers.Usb",
            "usb.bus:xhci0",
            "Zapada.Drivers.Hal",
            "pci:class:0c0330",
            DriverState.Loaded);

        DriverRegistry.Register(
            "usb-storage",
            "Zapada.Drivers.Usb",
            "block.device:sda",
            "usb.bus:xhci0,Zapada.Storage",
            "usb:08:06:50",
            DriverState.Loaded);

        Console.Write("[Boot] USB xHCI/mass-storage driver initialized\n");
        int registered = UsbStorageManager.RegisterManagedDevices();
        if (registered <= 0)
            Console.Write("[USB] no USB mass-storage block device registered\n");

        return 1;
    }
}
