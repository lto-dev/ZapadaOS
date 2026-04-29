using System;

namespace Zapada.Drivers;

public static class DriverManager
{
    public static string Resolve(string serviceKind, string bindTarget)
    {
        if (serviceKind == null || serviceKind.Length == 0)
            return null;

        int serviceCount = DriverRegistry.ServiceCount();
        for (int i = 0; i < serviceCount; i++)
        {
            ServiceDescriptor service = DriverRegistry.GetService(i);
            if (service == null)
                continue;

            if (!ServiceMatches(service, serviceKind, bindTarget))
                continue;

            DriverDescriptor provider = DriverRegistry.FindByKey(service.ProviderDriverKey);
            if (provider != null && provider.State != DriverState.Failed && provider.State != DriverState.Stopped)
                return provider.Key;
        }

        int driverCount = DriverRegistry.Count();
        for (int i = 0; i < driverCount; i++)
        {
            DriverDescriptor descriptor = DriverRegistry.Get(i);
            if (descriptor == null || descriptor.State == DriverState.Failed || descriptor.State == DriverState.Stopped)
                continue;

            if (ProvidesService(descriptor, serviceKind, bindTarget))
                return descriptor.Key;
        }

        return null;
    }

    public static int Start(string driverKey)
    {
        if (driverKey == null || driverKey.Length == 0)
            return -1;

        DriverDescriptor descriptor = DriverRegistry.FindByKey(driverKey);
        if (descriptor == null)
            return -2;

        if (descriptor.State == DriverState.Started)
            return 0;

        if (descriptor.State == DriverState.Failed)
            DriverRegistry.SetState(driverKey, DriverState.Loaded);

        int dependencyResult = StartDependencies(descriptor);
        if (dependencyResult != 0)
            return dependencyResult;

        DriverRegistry.SetState(driverKey, DriverState.Started);
        return 0;
    }

    public static int ProbeDependencies()
    {
        int failures = 0;
        int count = DriverRegistry.Count();
        for (int i = 0; i < count; i++)
        {
            DriverDescriptor descriptor = DriverRegistry.Get(i);
            if (descriptor == null)
                continue;

            if (!DependenciesSatisfied(descriptor.Requires))
            {
                failures++;
                DriverRegistry.SetFailed(descriptor.Key, "missing dependency");
            }
        }

        return failures;
    }

    public static int PrintDependencyPlan()
    {
        int failures = 0;
        Console.Write("driver dependencies\n");
        int count = DriverRegistry.Count();
        for (int i = 0; i < count; i++)
        {
            DriverDescriptor descriptor = DriverRegistry.Get(i);
            if (descriptor == null)
                continue;

            bool ready = DependenciesSatisfied(descriptor.Requires);
            if (!ready)
                failures++;

            Console.Write(descriptor.Key);
            Console.Write(" requires=");
            Console.Write(NormalizeEmpty(descriptor.Requires));
            Console.Write(" ready=");
            Console.Write(ready ? "yes" : "no");
            if (descriptor.FailureReason != null && descriptor.FailureReason.Length != 0)
            {
                Console.Write(" reason=");
                Console.Write(descriptor.FailureReason);
            }
            Console.Write("\n");
        }

        return failures == 0 ? 0 : -1;
    }

    public static int PrintDriverTree()
    {
        Console.Write("driver tree\n");
        int count = DriverRegistry.Count();
        for (int i = 0; i < count; i++)
        {
            DriverDescriptor descriptor = DriverRegistry.Get(i);
            if (descriptor == null)
                continue;

            Console.Write(descriptor.Key);
            Console.Write(" state=");
            Console.Write(DriverState.ToText(descriptor.State));
            Console.Write(" requires=");
            Console.Write(NormalizeEmpty(descriptor.Requires));
            Console.Write("\n");

            PrintDependencyProviders(descriptor.Requires);
        }

        return 0;
    }

    public static bool DependenciesSatisfied(string requires)
    {
        if (requires == null || requires.Length == 0)
            return true;

        int start = 0;
        while (start < requires.Length)
        {
            int end = start;
            while (end < requires.Length && requires[end] != ',')
                end++;

            if (end > start && !RequirementSatisfied(requires, start, end - start))
                return false;

            start = end + 1;
        }

        return true;
    }

    private static bool RequirementSatisfied(string text, int start, int length)
    {
        string requirement = text.Substring(start, length);
        if (requirement == null || requirement.Length == 0)
            return true;

        if (requirement == "Zapada.Drivers.Hal")
            return true;
        if (requirement == "Zapada.Storage")
            return DriverStarted("storage");
        if (requirement == "Zapada.Storage.PartitionView")
            return DriverStarted("storage") && DriverStarted("gpt");
        if (requirement == "Zapada.Storage.MountedVolume")
            return DriverStarted("storage");
        if (requirement == "Zapada.Fs.Vfs")
            return DriverStarted("vfs");
        if (requirement == "console")
            return true;
        if (StartsWith(requirement, "service:"))
            return HasProvider(requirement.Substring(8, requirement.Length - 8));

        return true;
    }

    private static int StartDependencies(DriverDescriptor descriptor)
    {
        if (descriptor == null || descriptor.Requires == null || descriptor.Requires.Length == 0)
            return 0;

        string requires = descriptor.Requires;
        int start = 0;
        while (start < requires.Length)
        {
            int end = start;
            while (end < requires.Length && requires[end] != ',')
                end++;

            int tokenStart = TrimTokenLeft(requires, start, end);
            int tokenEnd = TrimTokenRight(requires, tokenStart, end);
            if (tokenEnd > tokenStart)
            {
                string requirement = requires.Substring(tokenStart, tokenEnd - tokenStart);
                if (!RequirementSatisfied(requirement, 0, requirement.Length))
                {
                    string dependencyKey = ResolveRequirement(requirement);
                    if (dependencyKey == null || dependencyKey.Length == 0)
                    {
                        DriverRegistry.SetFailed(descriptor.Key, "missing dependency: " + requirement);
                        return -3;
                    }

                    if (dependencyKey != descriptor.Key)
                    {
                        int rc = Start(dependencyKey);
                        if (rc != 0)
                        {
                            DriverRegistry.SetFailed(descriptor.Key, "dependency failed: " + dependencyKey);
                            return rc;
                        }
                    }
                }
            }

            start = end + 1;
        }

        return 0;
    }

    private static string ResolveRequirement(string requirement)
    {
        if (requirement == null || requirement.Length == 0)
            return null;

        if (requirement == "Zapada.Storage")
            return "storage";
        if (requirement == "Zapada.Storage.PartitionView")
            return "gpt";
        if (requirement == "Zapada.Storage.MountedVolume")
            return "storage";
        if (requirement == "Zapada.Fs.Vfs")
            return "vfs";
        if (StartsWith(requirement, "service:"))
            return Resolve(requirement.Substring(8, requirement.Length - 8), "");

        return null;
    }

    private static bool DriverStarted(string key)
    {
        DriverDescriptor descriptor = DriverRegistry.FindByKey(key);
        return descriptor != null && descriptor.State == DriverState.Started;
    }

    private static bool HasProvider(string service)
    {
        ServiceDescriptor serviceDescriptor = DriverRegistry.FindService(service);
        if (serviceDescriptor != null)
            return DriverStarted(serviceDescriptor.ProviderDriverKey);

        int count = DriverRegistry.Count();
        for (int i = 0; i < count; i++)
        {
            DriverDescriptor descriptor = DriverRegistry.Get(i);
            if (descriptor == null || descriptor.State != DriverState.Started)
                continue;

            if (ContainsToken(descriptor.Provides, service))
                return true;
        }

        return false;
    }

    private static bool ProvidesService(DriverDescriptor descriptor, string serviceKind, string bindTarget)
    {
        if (descriptor == null || descriptor.Provides == null || descriptor.Provides.Length == 0)
            return false;

        int start = 0;
        while (start < descriptor.Provides.Length)
        {
            int end = start;
            while (end < descriptor.Provides.Length && descriptor.Provides[end] != ',')
                end++;

            int tokenStart = TrimTokenLeft(descriptor.Provides, start, end);
            int tokenEnd = TrimTokenRight(descriptor.Provides, tokenStart, end);
            if (tokenEnd > tokenStart && ServiceTokenMatches(descriptor.Provides.Substring(tokenStart, tokenEnd - tokenStart), serviceKind, bindTarget))
                return true;

            start = end + 1;
        }

        return false;
    }

    private static bool ServiceMatches(ServiceDescriptor service, string serviceKind, string bindTarget)
    {
        if (service == null)
            return false;

        if (service.Kind == serviceKind)
        {
            if (bindTarget == null || bindTarget.Length == 0 || bindTarget == "*")
                return true;

            return service.DisplayName == bindTarget || service.Key == serviceKind + ":" + bindTarget;
        }

        return service.Key == serviceKind;
    }

    private static bool ServiceTokenMatches(string token, string serviceKind, string bindTarget)
    {
        if (token == null || serviceKind == null)
            return false;

        if (token == serviceKind)
            return bindTarget == null || bindTarget.Length == 0 || bindTarget == "*";

        if (StartsWith(token, serviceKind + ":"))
        {
            if (bindTarget == null || bindTarget.Length == 0 || bindTarget == "*")
                return true;

            string displayName = token.Substring(serviceKind.Length + 1, token.Length - serviceKind.Length - 1);
            return displayName == bindTarget;
        }

        return false;
    }

    private static void PrintDependencyProviders(string requires)
    {
        if (requires == null || requires.Length == 0)
            return;

        int start = 0;
        while (start < requires.Length)
        {
            int end = start;
            while (end < requires.Length && requires[end] != ',')
                end++;

            int tokenStart = TrimTokenLeft(requires, start, end);
            int tokenEnd = TrimTokenRight(requires, tokenStart, end);
            if (tokenEnd > tokenStart)
            {
                string requirement = requires.Substring(tokenStart, tokenEnd - tokenStart);
                Console.Write("  -> ");
                Console.Write(requirement);
                Console.Write(" provider=");
                string provider = ResolveRequirement(requirement);
                Console.Write(provider == null || provider.Length == 0 ? "built-in" : provider);
                Console.Write(" ready=");
                Console.Write(RequirementSatisfied(requirement, 0, requirement.Length) ? "yes" : "no");
                Console.Write("\n");
            }

            start = end + 1;
        }
    }

    private static bool ContainsToken(string text, string value)
    {
        if (text == null || value == null || value.Length == 0)
            return false;

        int start = 0;
        while (start < text.Length)
        {
            int end = start;
            while (end < text.Length && text[end] != ',')
                end++;

            if (end - start == value.Length)
            {
                bool match = true;
                for (int i = 0; i < value.Length; i++)
                {
                    if (text[start + i] != value[i])
                    {
                        match = false;
                        break;
                    }
                }

                if (match)
                    return true;
            }

            start = end + 1;
        }

        return false;
    }

    private static bool StartsWith(string text, string prefix)
    {
        if (text == null || prefix == null || text.Length < prefix.Length)
            return false;

        for (int i = 0; i < prefix.Length; i++)
        {
            if (text[i] != prefix[i])
                return false;
        }

        return true;
    }

    private static string NormalizeEmpty(string value)
    {
        if (value == null || value.Length == 0)
            return "none";

        return value;
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
}
