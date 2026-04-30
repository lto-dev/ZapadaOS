namespace Zapada.Storage;

public static class EntropyService
{
    private static uint s_stateA;
    private static uint s_stateB;
    private static int s_initialized;
    private static int s_fillCount;

    public static void Initialize(uint seedA, uint seedB)
    {
        s_stateA = seedA ^ 0xA5A55A5Au;
        s_stateB = seedB ^ 0xC3C33C3Cu;

        if (s_stateA == 0u)
            s_stateA = 0x6D2B79F5u;
        if (s_stateB == 0u)
            s_stateB = 0x1B873593u;

        Mix((uint)DeviceRegistry.Count());
        Mix((uint)BlockDeviceRegistry.Count());
        Mix((uint)PartitionRegistry.Count());
        s_initialized = 1;
        s_fillCount = 0;
    }

    public static int Fill(byte[] buffer, int offset, int count)
    {
        if (buffer == null || offset < 0 || count < 0 || offset + count > buffer.Length)
            return StorageStatus.InvalidArgument;

        EnsureInitialized();

        for (int i = 0; i < count; i++)
            buffer[offset + i] = (byte)(NextByte() & 0xFF);

        s_fillCount++;
        return count;
    }

    public static int NextByte()
    {
        EnsureInitialized();

        uint x = s_stateA;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s_stateA = x;

        s_stateB = (s_stateB * 1664525u) + 1013904223u + (uint)s_fillCount;
        return (int)((s_stateA ^ s_stateB ^ (s_stateA >> 11) ^ (s_stateB << 7)) & 0xFFu);
    }

    public static int FillFromDevice(int targetHandle, byte[] buffer, int offset, int count)
    {
        if (targetHandle == 2)
            Mix(0x52414E44u);
        else
            Mix(0x5552414Eu);

        return Fill(buffer, offset, count);
    }

    public static int FillCount()
    {
        return s_fillCount;
    }

    public static string QualityDescription()
    {
        return "provisional non-cryptographic entropy";
    }

    private static void EnsureInitialized()
    {
        if (s_initialized != 0)
            return;

        Initialize(0x13579BDFu, 0x2468ACE0u);
    }

    private static void Mix(uint value)
    {
        s_stateA ^= value + 0x9E3779B9u + (s_stateA << 6) + (s_stateA >> 2);
        s_stateB ^= (value << 16) | (value >> 16);
        s_stateB = (s_stateB * 1103515245u) + 12345u;
    }
}
