namespace Zapada.Drivers;

public static class DriverState
{
    public const int Discovered = 1;
    public const int Loaded = 2;
    public const int Bound = 3;
    public const int Started = 4;
    public const int Failed = 5;
    public const int Stopped = 6;

    public static string ToText(int state)
    {
        if (state == Discovered)
            return "discovered";
        if (state == Loaded)
            return "loaded";
        if (state == Bound)
            return "bound";
        if (state == Started)
            return "started";
        if (state == Failed)
            return "failed";
        if (state == Stopped)
            return "stopped";

        return "unknown";
    }
}
