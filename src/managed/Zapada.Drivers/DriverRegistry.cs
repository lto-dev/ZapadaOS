using System;

namespace Zapada.Drivers;

public static class DriverRegistry
{
    private const int MaxDrivers = 32;
    private const int MaxServices = 64;
    private static DriverDescriptor[] s_drivers;
    private static ServiceDescriptor[] s_services;
    private static int s_count;
    private static int s_serviceCount;

    public static void Initialize()
    {
        s_drivers = new DriverDescriptor[MaxDrivers];
        s_services = new ServiceDescriptor[MaxServices];
        s_count = 0;
        s_serviceCount = 0;
    }

    public static int Register(string key, string assemblyName, string provides, string requires, string bindsTo, int state)
    {
        if (key == null || key.Length == 0 || assemblyName == null || assemblyName.Length == 0)
            return -1;

        EnsureInitialized();

        DriverDescriptor existing = FindByKey(key);
        if (existing != null)
        {
            existing.AssemblyName = assemblyName;
            existing.Provides = provides;
            existing.Requires = requires;
            existing.BindsTo = bindsTo;
            existing.State = state;
            existing.FailureReason = "";
            RegisterProvidedServices(existing.Key, provides);
            return 1;
        }

        if (s_count >= MaxDrivers)
            return -2;

        DriverDescriptor descriptor = new DriverDescriptor();
        descriptor.Initialize(key, assemblyName, provides, requires, bindsTo, state);
        s_drivers[s_count] = descriptor;
        s_count++;
        RegisterProvidedServices(key, provides);
        return 0;
    }

    public static int RegisterService(string key, string kind, string providerDriverKey, string displayName)
    {
        if (key == null || key.Length == 0 || providerDriverKey == null || providerDriverKey.Length == 0)
            return -1;

        EnsureInitialized();

        if (kind == null || kind.Length == 0)
            kind = ServiceKindFromKey(key);
        if (displayName == null || displayName.Length == 0)
            displayName = DisplayNameFromKey(key);

        ServiceDescriptor existing = FindService(key);
        if (existing != null)
        {
            existing.Kind = kind;
            existing.ProviderDriverKey = providerDriverKey;
            existing.DisplayName = displayName;
            return 1;
        }

        if (s_serviceCount >= MaxServices)
            return -2;

        ServiceDescriptor service = new ServiceDescriptor();
        service.Initialize(key, kind, providerDriverKey, displayName);
        s_services[s_serviceCount] = service;
        s_serviceCount++;
        return 0;
    }

    public static int SetState(string key, int state)
    {
        DriverDescriptor descriptor = FindByKey(key);
        if (descriptor == null)
            return -1;

        descriptor.State = state;
        if (state != DriverState.Failed)
            descriptor.FailureReason = "";
        return 0;
    }

    public static int SetFailed(string key, string reason)
    {
        DriverDescriptor descriptor = FindByKey(key);
        if (descriptor == null)
            return -1;

        descriptor.State = DriverState.Failed;
        descriptor.FailureReason = reason == null ? "" : reason;
        return 0;
    }

    public static int AddUse(string key)
    {
        DriverDescriptor descriptor = FindByKey(key);
        if (descriptor == null)
            return -1;

        descriptor.UseCount++;
        return descriptor.UseCount;
    }

    public static int Count()
    {
        EnsureInitialized();
        return s_count;
    }

    public static DriverDescriptor Get(int index)
    {
        EnsureInitialized();

        if (index < 0 || index >= s_count)
            return null;

        return s_drivers[index];
    }

    public static DriverDescriptor FindByKey(string key)
    {
        EnsureInitialized();

        if (key == null)
            return null;

        for (int i = 0; i < s_count; i++)
        {
            DriverDescriptor descriptor = s_drivers[i];
            if (descriptor != null && descriptor.Key == key)
                return descriptor;
        }

        return null;
    }

    public static int ServiceCount()
    {
        EnsureInitialized();
        return s_serviceCount;
    }

    public static ServiceDescriptor GetService(int index)
    {
        EnsureInitialized();

        if (index < 0 || index >= s_serviceCount)
            return null;

        return s_services[index];
    }

    public static ServiceDescriptor FindService(string key)
    {
        EnsureInitialized();

        if (key == null)
            return null;

        for (int i = 0; i < s_serviceCount; i++)
        {
            ServiceDescriptor service = s_services[i];
            if (service != null && service.Key == key)
                return service;
        }

        return null;
    }

    public static int PrintDrivers()
    {
        EnsureInitialized();

        Console.Write("driver                 state       uses provides\n");
        for (int i = 0; i < s_count; i++)
        {
            DriverDescriptor descriptor = s_drivers[i];
            if (descriptor == null)
                continue;

            Console.Write(descriptor.Key);
            Console.Write(" ");
            PrintState(descriptor.State);
            Console.Write(" ");
            Console.Write(descriptor.UseCount);
            Console.Write(" ");
            Console.Write(descriptor.Provides);
            Console.Write("\n");
        }

        return 0;
    }

    public static int PrintDriversFull()
    {
        EnsureInitialized();

        Console.Write("driver                 state       uses provides requires binds\n");
        for (int i = 0; i < s_count; i++)
        {
            DriverDescriptor descriptor = s_drivers[i];
            if (descriptor == null)
                continue;

            Console.Write(descriptor.Key);
            Console.Write(" ");
            PrintState(descriptor.State);
            Console.Write(" ");
            Console.Write(descriptor.UseCount);
            Console.Write(" ");
            Console.Write(descriptor.Provides);
            Console.Write(" requires=");
            PrintValueOrNone(descriptor.Requires);
            Console.Write(" binds=");
            PrintValueOrNone(descriptor.BindsTo);
            if (descriptor.FailureReason != null && descriptor.FailureReason.Length != 0)
            {
                Console.Write(" reason=");
                Console.Write(descriptor.FailureReason);
            }
            Console.Write("\n");
        }

        return 0;
    }

    public static int PrintServices()
    {
        EnsureInitialized();

        Console.Write("service                kind provider display\n");
        for (int i = 0; i < s_serviceCount; i++)
        {
            ServiceDescriptor service = s_services[i];
            if (service == null)
                continue;

            Console.Write(service.Key);
            Console.Write(" ");
            Console.Write(service.Kind);
            Console.Write(" ");
            Console.Write(service.ProviderDriverKey);
            Console.Write(" ");
            Console.Write(service.DisplayName);
            Console.Write("\n");
        }

        return 0;
    }

    private static void EnsureInitialized()
    {
        if (s_drivers == null)
            Initialize();
    }

    private static void PrintState(int state)
    {
        Console.Write(DriverState.ToText(state));
    }

    private static void RegisterProvidedServices(string providerDriverKey, string provides)
    {
        if (providerDriverKey == null || providerDriverKey.Length == 0 || provides == null || provides.Length == 0)
            return;

        int start = 0;
        while (start < provides.Length)
        {
            int end = start;
            while (end < provides.Length && provides[end] != ',')
                end++;

            int tokenStart = TrimTokenLeft(provides, start, end);
            int tokenEnd = TrimTokenRight(provides, tokenStart, end);
            if (tokenEnd > tokenStart)
            {
                string key = provides.Substring(tokenStart, tokenEnd - tokenStart);
                RegisterService(key, ServiceKindFromKey(key), providerDriverKey, DisplayNameFromKey(key));
            }

            start = end + 1;
        }
    }

    private static string ServiceKindFromKey(string key)
    {
        if (key == null || key.Length == 0)
            return "unknown";

        int colon = FindChar(key, ':');
        if (colon > 0)
            return key.Substring(0, colon);

        return key;
    }

    private static string DisplayNameFromKey(string key)
    {
        if (key == null || key.Length == 0)
            return "unknown";

        int colon = FindChar(key, ':');
        if (colon >= 0 && colon + 1 < key.Length)
            return key.Substring(colon + 1, key.Length - colon - 1);

        return key;
    }

    private static int FindChar(string text, char value)
    {
        if (text == null)
            return -1;

        for (int i = 0; i < text.Length; i++)
        {
            if (text[i] == value)
                return i;
        }

        return -1;
    }

    private static int TrimTokenLeft(string text, int start, int end)
    {
        int pos = start;
        while (pos < end && (text[pos] == ' ' || text[pos] == '\t'))
            pos++;

        return pos;
    }

    private static int TrimTokenRight(string text, int start, int end)
    {
        int pos = end;
        while (pos > start && (text[pos - 1] == ' ' || text[pos - 1] == '\t'))
            pos--;

        return pos;
    }

    private static void PrintValueOrNone(string value)
    {
        if (value == null || value.Length == 0)
        {
            Console.Write("none");
            return;
        }

        Console.Write(value);
    }
}
