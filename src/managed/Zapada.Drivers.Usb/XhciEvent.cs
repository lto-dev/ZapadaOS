namespace Zapada.Drivers.Usb;

internal sealed class XhciEvent
{
    public long Parameter;
    public int Status;
    public int Control;
    public int Type;
    public int CompletionCode;
    public int SlotId;
    public int EndpointId;
}
