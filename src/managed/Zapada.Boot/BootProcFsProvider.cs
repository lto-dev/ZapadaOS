using Zapada.Drivers;
using Zapada.Fs.Vfs;
using Zapada.Storage;

namespace Zapada.Boot;

internal sealed class BootProcFsProvider : ProcFsProvider
{
    public override string BuildMounts()
    {
        string text = "mount driver label name\n";
        int count = Vfs.MountCount;
        for (int i = 0; i < count; i++)
        {
            string path = Vfs.GetMountPath(i);
            if (path == null || path.Length == 0)
                continue;

            text = AppendField(text, path);
            text = AppendField(text, Vfs.GetMountDriverKey(i));
            text = AppendField(text, Vfs.GetMountVolumeLabel(i));
            text = AppendLine(text, Vfs.GetMountDisplayName(i));
        }

        return text;
    }

    public override string BuildDrivers()
    {
        string text = "driver state uses provides requires binds\n";
        int count = Zapada.Drivers.DriverRegistry.Count();
        for (int i = 0; i < count; i++)
        {
            DriverDescriptor descriptor = Zapada.Drivers.DriverRegistry.Get(i);
            if (descriptor == null)
                continue;

            text = AppendField(text, descriptor.Key);
            text = AppendField(text, DriverState.ToText(descriptor.State));
            text = AppendField(text, IntToString(descriptor.UseCount));
            text = AppendField(text, ValueOrDash(descriptor.Provides));
            text = AppendField(text, ValueOrDash(descriptor.Requires));
            text = AppendLine(text, ValueOrDash(descriptor.BindsTo));
        }

        return text;
    }

    public override string BuildDevices()
    {
        string text = "device kind service driver target\n";
        int count = DeviceRegistry.Count();
        for (int i = 0; i < count; i++)
        {
            DeviceNode node = DeviceRegistry.Get(i);
            if (node == null)
                continue;

            text = AppendField(text, node.Path);
            text = AppendField(text, DeviceRegistry.KindToString(node.Kind));
            text = AppendField(text, ValueOrDash(node.ServiceKey));
            text = AppendField(text, ValueOrDash(node.DriverKey));
            text = AppendLine(text, IntToString(node.TargetHandle));
        }

        return text;
    }

    public override string BuildPartitions()
    {
        string text = "partition device label start sectors scheme\n";
        int count = PartitionRegistry.Count();
        for (int i = 0; i < count; i++)
        {
            PartitionInfo partition = PartitionRegistry.Get(i);
            if (partition == null)
                continue;

            text = AppendField(text, partition.Name);
            text = AppendField(text, partition.DeviceName);
            text = AppendField(text, partition.Label);
            text = AppendField(text, LongToString(partition.StartLba));
            text = AppendField(text, LongToString(partition.SectorCount));
            text = AppendLine(text, partition.SchemeKind == 2 ? "gpt" : "unknown");
        }

        return text;
    }

    public override string BuildInterrupts()
    {
        string text = "irq source channel events status\n";
        text = string.Concat(text, "0 timer channel-delivered provisional tracked-by-driver-hal\n");
        return text;
    }

    public override string BuildMemInfo()
    {
        string text = "name bytes source\n";
        text = string.Concat(text, "managed_live unknown no-public-runtime-counter\n");
        text = string.Concat(text, "managed_free unknown no-public-runtime-counter\n");
        return text;
    }

    public override string BuildUptime()
    {
        string text = "uptime_seconds idle_seconds source\n";
        text = string.Concat(text, "0 0 provisional-no-public-tick-counter\n");
        return text;
    }

    private static string AppendField(string text, string value)
    {
        text = string.Concat(text, ValueOrDash(value));
        return string.Concat(text, " ");
    }

    private static string AppendLine(string text, string value)
    {
        text = string.Concat(text, ValueOrDash(value));
        return string.Concat(text, "\n");
    }

    private static string ValueOrDash(string value)
    {
        if (value == null || value.Length == 0)
            return "-";

        return value;
    }

    private static string IntToString(int value)
    {
        return LongToString(value);
    }

    private static string LongToString(long value)
    {
        if (value == 0)
            return "0";

        if (value < 0)
            return string.Concat("-", LongToString(0 - value));

        string text = "";
        long current = value;
        while (current > 0)
        {
            int digit = (int)(current % 10);
            text = string.Concat(DigitToString(digit), text);
            current = current / 10;
        }

        return text;
    }

    private static string DigitToString(int digit)
    {
        if (digit == 0) return "0";
        if (digit == 1) return "1";
        if (digit == 2) return "2";
        if (digit == 3) return "3";
        if (digit == 4) return "4";
        if (digit == 5) return "5";
        if (digit == 6) return "6";
        if (digit == 7) return "7";
        if (digit == 8) return "8";
        return "9";
    }
}
