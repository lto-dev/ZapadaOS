namespace Zapada.Storage;

public sealed class DeviceNode
{
    public string Path = "";
    public string Name = "";
    public int Kind;
    public string ServiceKey = "";
    public string DriverKey = "";
    public int Permissions;
    public int TargetHandle;

    public void Initialize(string path, string name, int kind, string serviceKey, string driverKey, int permissions, int targetHandle)
    {
        Path = path;
        Name = name;
        Kind = kind;
        ServiceKey = serviceKey;
        DriverKey = driverKey;
        Permissions = permissions;
        TargetHandle = targetHandle;
    }
}
