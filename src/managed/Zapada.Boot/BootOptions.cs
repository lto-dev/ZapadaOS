namespace Zapada.Boot;

internal static class BootOptions
{
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
