namespace Zapada.Drivers;

public sealed class ServiceDescriptor
{
    public string Key;
    public string Kind;
    public string ProviderDriverKey;
    public string DisplayName;

    public void Initialize(string key, string kind, string providerDriverKey, string displayName)
    {
        Key = key;
        Kind = kind;
        ProviderDriverKey = providerDriverKey;
        DisplayName = displayName;
    }
}
