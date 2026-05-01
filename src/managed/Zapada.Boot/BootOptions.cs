namespace Zapada.Boot;

internal static class BootOptions
{
    private const string DefaultInitTarget = "/sbin/Zapada.Minid.dll";
    private const string DefaultRunlevel = "3";

    internal static string CommandLine()
    {
        string commandLine = Zapada.Kernel.GetBootCommandLine();
        if (commandLine == null)
            return "";

        return commandLine;
    }

    internal static bool IsSmokeMode()
    {
        return HasToken(CommandLine(), "--smoke");
    }

    internal static string InitTarget()
    {
        string value = GetOptionValue(CommandLine(), "init=");
        if (value == null || value.Length == 0)
            return DefaultInitTarget;

        return value;
    }

    internal static string Runlevel()
    {
        if (IsEmergencyMode())
            return "emergency";
        if (IsSingleUserMode())
            return "1";

        string value = GetOptionValue(CommandLine(), "runlevel=");
        if (value == null || value.Length == 0)
            return DefaultRunlevel;

        return value;
    }

    internal static bool IsEmergencyMode()
    {
        string commandLine = CommandLine();
        return HasToken(commandLine, "emergency") || HasToken(commandLine, "--emergency");
    }

    internal static bool IsSingleUserMode()
    {
        string commandLine = CommandLine();
        return HasToken(commandLine, "single") || HasToken(commandLine, "--single") || HasToken(commandLine, "-s");
    }

    internal static bool RequestsShellInit()
    {
        string initTarget = InitTarget();
        return initTarget == "/Zapada.Shell.dll" || initTarget == "/bin/Zapada.Shell.dll" || initTarget == "Zapada.Shell.dll" || initTarget == "shell";
    }

    internal static bool RequestsMinidInit()
    {
        string initTarget = InitTarget();
        return initTarget == "/Zapada.Minid.dll" || initTarget == "/sbin/Zapada.Minid.dll" || initTarget == "Zapada.Minid.dll" || initTarget == "minid" || initTarget == "init";
    }

    private static bool HasToken(string text, string token)
    {
        if (text == null || token == null || token.Length == 0)
            return false;

        int length = text.Length;
        int pos = 0;
        while (pos < length)
        {
            while (pos < length && IsSpace(text[pos]))
                pos++;

            int start = pos;
            while (pos < length && !IsSpace(text[pos]))
                pos++;

            int tokenLength = pos - start;
            if (TokenEquals(text, start, tokenLength, token))
                return true;
        }

        return false;
    }

    private static string GetOptionValue(string text, string prefix)
    {
        if (text == null || prefix == null || prefix.Length == 0)
            return "";

        int length = text.Length;
        int pos = 0;
        while (pos < length)
        {
            while (pos < length && IsSpace(text[pos]))
                pos++;

            int start = pos;
            while (pos < length && !IsSpace(text[pos]))
                pos++;

            int tokenLength = pos - start;
            if (TokenStartsWith(text, start, tokenLength, prefix))
                return text.Substring(start + prefix.Length, tokenLength - prefix.Length);
        }

        return "";
    }

    private static bool TokenStartsWith(string text, int start, int length, string expectedPrefix)
    {
        if (length < expectedPrefix.Length)
            return false;

        for (int i = 0; i < expectedPrefix.Length; i++)
        {
            if (text[start + i] != expectedPrefix[i])
                return false;
        }

        return true;
    }

    private static bool TokenEquals(string text, int start, int length, string expected)
    {
        if (length != expected.Length)
            return false;

        for (int i = 0; i < length; i++)
        {
            if (text[start + i] != expected[i])
                return false;
        }

        return true;
    }

    private static bool IsSpace(char value)
    {
        return value == ' ' || value == '\t' || value == '\r' || value == '\n';
    }
}
