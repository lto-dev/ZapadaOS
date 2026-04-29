namespace Zapada.Drivers;

public sealed class DriverManifest
{
    public string Key;
    public string AssemblyName;
    public string Provides;
    public string Requires;
    public string BindsTo;

    public void Initialize(string key, string assemblyName, string provides, string requires, string bindsTo)
    {
        Key = key;
        AssemblyName = assemblyName;
        Provides = provides;
        Requires = requires;
        BindsTo = bindsTo;
    }
}
