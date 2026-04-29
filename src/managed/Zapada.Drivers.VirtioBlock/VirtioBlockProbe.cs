using System;
using Zapada.Drivers.Hal;

namespace Zapada.Drivers;

internal static class VirtioBlockProbe
{
    private const int VirtioVendorId = 0x1AF4;
    private const int VirtioBlockLegacyDeviceId = 0x1001;
    private const int VirtioBlockModernDeviceId = 0x1042;
    private const int QemuVgaVendorId = 0x1234;
    private const int QemuVgaDeviceId = 0x1111;

    public static int RunSmoke()
    {
        int channel = DriverHal.CreateChannel();
        if (channel <= 0)
        {
            Console.Write("[DriverHal] channel create failed\n");
            return 0;
        }

        byte[] payload = new byte[DriverHal.IpcMessagePayloadMax];
        payload[0] = 0x5A;
        payload[1] = 0x50;

        int sendRc = DriverHal.TrySend(channel, DriverHal.IpcMessageUser + 1, payload, 2);
        if (sendRc != 0)
        {
            Console.Write("[DriverHal] channel send failed rc=");
            Console.Write(sendRc);
            Console.Write("\n");
            DriverHal.DestroyChannel(channel);
            return 0;
        }

        byte[] receive = new byte[DriverHal.IpcMessagePayloadMax];
        int recvRc = DriverHal.TryReceive(channel, DriverHal.IpcMessageAny, receive);
        if (recvRc != 0)
        {
            Console.Write("[DriverHal] channel receive failed rc=");
            Console.Write(recvRc);
            Console.Write("\n");
            DriverHal.DestroyChannel(channel);
            return 0;
        }

        DriverHal.DestroyChannel(channel);

        int buffer = DriverHal.AllocBuffer(512);
        if (buffer <= 0)
        {
            Console.Write("[DriverHal] buffer allocation failed\n");
            return 0;
        }

        int bufferSize = DriverHal.BufferSize(buffer);
        if (bufferSize != 512)
        {
            Console.Write("[DriverHal] buffer size mismatch size=");
            Console.Write(bufferSize);
            Console.Write("\n");
            DriverHal.FreeBuffer(buffer);
            return 0;
        }
        DriverHal.FreeBuffer(buffer);

        int pciCount = PciBus.DeviceCount();
        if (pciCount <= 0)
        {
            Console.Write("[DriverHal] PCI inventory empty\n");
            return 0;
        }

        PciDeviceInfo inventoryDevice = PciBus.FindDevice(VirtioVendorId, VirtioBlockModernDeviceId);
        if (inventoryDevice == null)
            inventoryDevice = PciBus.FindDevice(VirtioVendorId, VirtioBlockLegacyDeviceId);

        if (inventoryDevice == null)
        {
            Console.Write("[DriverHal] VirtIO block device missing from PCI inventory\n");
            return 0;
        }

        int device = DriverHal.PciFindDevice(VirtioVendorId, VirtioBlockModernDeviceId);
        if (device < 0)
            device = DriverHal.PciFindDevice(VirtioVendorId, VirtioBlockLegacyDeviceId);

        if (device < 0)
        {
            Console.Write("[DriverHal] VirtIO block PCI device not found\n");
            return 0;
        }

        int id = DriverHal.PciReadConfig32(device, 0);
        int bar0 = DriverHal.PciReadBar32(device, 0);
        if (id == -1 || bar0 == -1 || inventoryDevice.Handle != device || inventoryDevice.Bar0 != bar0)
        {
            Console.Write("[DriverHal] VirtIO block config read failed\n");
            return 0;
        }

        int mmioSmoke = SmokeMmioRegion(device, bar0);
        if (mmioSmoke == 0)
            return 0;

        int qemuVgaSmoke = SmokeKnownVgaMmioRead();
        if (qemuVgaSmoke != DriverHal.StatusSuccess && qemuVgaSmoke != DriverHal.StatusUnsupported)
        {
            Console.Write("[DriverHal] QEMU VGA MMIO smoke failed rc=");
            Console.Write(qemuVgaSmoke);
            Console.Write("\n");
            return 0;
        }

        int dmaSmoke = SmokeDmaBuffer();
        if (dmaSmoke == 0)
            return 0;

        int irqSmoke = SmokeIrqChannel();
        if (irqSmoke == 0)
            return 0;

        Console.Write("[Gate] Phase-DriverHal\n");
        return 1;
    }

    private static int SmokeIrqChannel()
    {
        int channel = DriverHal.CreateChannel();
        if (channel <= 0)
        {
            Console.Write("[DriverHal] IRQ channel create failed\n");
            return 0;
        }

        IrqSubscription subscription = IrqSubscription.Subscribe(DriverHal.IrqTimer, channel);
        if (subscription == null)
        {
            Console.Write("[DriverHal] IRQ subscribe failed\n");
            DriverHal.DestroyChannel(channel);
            return 0;
        }

        byte[] receive = new byte[DriverHal.IpcMessagePayloadMax];
        for (int attempt = 0; attempt < 4096; attempt++)
        {
            int rc = DriverHal.TryReceive(channel, DriverHal.IpcMessageIrq, receive);
            if (rc == 0)
            {
                int messageType = ReadInt32(receive, 0);
                int payloadLength = ReadInt32(receive, 8);
                int irqNumber = ReadInt32(receive, 16);
                int unsubscribeRc = subscription.Unsubscribe();
                DriverHal.DestroyChannel(channel);

                if (messageType != DriverHal.IpcMessageIrq || payloadLength < 16 || irqNumber != DriverHal.IrqTimer || unsubscribeRc != DriverHal.StatusSuccess)
                {
                    Console.Write("[DriverHal] IRQ event payload invalid\n");
                    return 0;
                }

                return 1;
            }

            if (rc != DriverHal.IpcErrEmpty)
            {
                Console.Write("[DriverHal] IRQ receive failed rc=");
                Console.Write(rc);
                Console.Write("\n");
                subscription.Unsubscribe();
                DriverHal.DestroyChannel(channel);
                return 0;
            }
        }

        Console.Write("[DriverHal] IRQ timer event not observed\n");
        subscription.Unsubscribe();
        DriverHal.DestroyChannel(channel);
        return 0;
    }

    private static int SmokeDmaBuffer()
    {
        DmaBuffer buffer = DmaBuffer.Allocate(4096);
        if (buffer == null)
        {
            Console.Write("[DriverHal] DMA allocation failed\n");
            return 0;
        }

        if (buffer.Size < 4096 || buffer.PhysicalAddress == 0 || (buffer.PhysicalAddress & 4095) != 0)
        {
            Console.Write("[DriverHal] DMA buffer metadata invalid\n");
            buffer.Free();
            return 0;
        }

        int writeRc = buffer.Write32(0, 0x5A504441);
        int readValue = buffer.Read32(0);
        int outOfBounds = buffer.Read32(buffer.Size);
        if (writeRc != DriverHal.StatusSuccess || readValue != 0x5A504441 || outOfBounds != DriverHal.StatusInvalid)
        {
            Console.Write("[DriverHal] DMA buffer read/write failed\n");
            buffer.Free();
            return 0;
        }

        if (buffer.Free() != DriverHal.StatusSuccess)
        {
            Console.Write("[DriverHal] DMA free failed\n");
            return 0;
        }

        return 1;
    }

    private static int SmokeKnownVgaMmioRead()
    {
        PciDeviceInfo display = PciBus.FindDevice(QemuVgaVendorId, QemuVgaDeviceId);
        if (display == null)
            return DriverHal.StatusUnsupported;

        MmioRegion region = PciBus.OpenBar(display.Handle, 0);
        if (region == null)
            return DriverHal.StatusUnsupported;

        if (region.Size < 4)
        {
            region.Close();
            return DriverHal.StatusInvalid;
        }

        region.Read32(0);
        region.Close();
        return DriverHal.StatusSuccess;
    }

    private static int SmokeMmioRegion(int device, int bar0)
    {
        for (int bar = 0; bar < 6; bar++)
        {
            MmioRegion region = PciBus.OpenBar(device, bar);
            if (region == null)
                continue;

            if (region.Size < 4)
            {
                Console.Write("[DriverHal] MMIO region size invalid\n");
                region.Close();
                return 0;
            }

            int outOfBounds = region.Read32(region.Size);
            if (outOfBounds != DriverHal.StatusInvalid)
            {
                Console.Write("[DriverHal] MMIO bounds check failed\n");
                region.Close();
                return 0;
            }

            region.Close();
            return 1;
        }

        if ((bar0 & 1) != 0)
        {
            int bar0Open = DriverHal.PciOpenBar(device, 0);
            if (bar0Open == DriverHal.StatusUnsupported)
                return 1;
        }

        Console.Write("[DriverHal] VirtIO block MMIO BAR unavailable\n");
        return 0;
    }

    private static int ReadInt32(byte[] buffer, int offset)
    {
        return (buffer[offset] & 0xFF)
             | ((buffer[offset + 1] & 0xFF) << 8)
             | ((buffer[offset + 2] & 0xFF) << 16)
             | ((buffer[offset + 3] & 0xFF) << 24);
    }
}
