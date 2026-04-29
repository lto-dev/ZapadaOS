namespace Zapada.Drivers;

public sealed class DriverDescriptor
{
    public string Key;
    public string AssemblyName;
    public string Provides;
    public string Requires;
    public string BindsTo;
    public int State;
    public int UseCount;
    public string FailureReason;

    public void Initialize(string key, string assemblyName, string provides, string requires, string bindsTo, int state)
    {
        Key = key;
        AssemblyName = assemblyName;
        Provides = provides;
        Requires = requires;
        BindsTo = bindsTo;
        State = state;
        UseCount = 0;
        FailureReason = "";
    }
}
