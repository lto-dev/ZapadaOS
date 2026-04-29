namespace Zapada.Drivers.Hal;

public sealed class IrqSubscription
{
    public int Handle;
    public int IrqNumber;
    public int ChannelHandle;

    public static IrqSubscription Subscribe(int irqNumber, int channelHandle)
    {
        int handle = DriverHal.SubscribeIrq(irqNumber, channelHandle);
        if (handle <= 0)
            return null;

        IrqSubscription subscription = new IrqSubscription();
        subscription.Handle = handle;
        subscription.IrqNumber = irqNumber;
        subscription.ChannelHandle = channelHandle;
        return subscription;
    }

    public int Unsubscribe()
    {
        int handle = Handle;
        Handle = 0;
        IrqNumber = 0;
        ChannelHandle = 0;
        return DriverHal.UnsubscribeIrq(handle);
    }
}
