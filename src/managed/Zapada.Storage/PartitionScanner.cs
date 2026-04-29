namespace Zapada.Storage;

public static class PartitionScanner
{
    private const int GptSignatureLo = 0x20494645;
    private const int GptSignatureHi = 0x54524150;
    private const int GptEntrySize = 128;
    private const int GptEntriesPerSector = 4;

    public static int ScanAllBlockDevices()
    {
        PartitionRegistry.Initialize();

        int found = 0;
        int count = BlockDeviceRegistry.Count();
        for (int i = 0; i < count; i++)
        {
            BlockDevice? device = BlockDeviceRegistry.Get(i);
            if (device == null)
                continue;

            int rc = ScanGpt(device);
            if (rc > 0)
                found += rc;
        }

        return found;
    }

    public static int ScanGpt(BlockDevice device)
    {
        if (device == null)
            return StorageStatus.InvalidArgument;

        BlockDeviceInfo info = device.GetInfo();
        if (info == null || info.Name == null || info.Name.Length == 0)
            return StorageStatus.InvalidArgument;

        byte[] header = new byte[512];
        int rc = device.ReadSectors(1L, 1, header, 0);
        if (rc < 0)
            return rc;
        if (rc != 1)
            return StorageStatus.IoError;

        if (GetDword(header, 0) != GptSignatureLo || GetDword(header, 4) != GptSignatureHi)
            return 0;

        long entryLba = GetQword(header, 72);
        int entryCount = GetDword(header, 80);
        int entrySize = GetDword(header, 84);
        if (entryLba <= 0 || entryCount <= 0 || entrySize != GptEntrySize)
            return StorageStatus.CorruptedData;

        byte[] entries = new byte[512];
        int sectorIndex = 0;
        int entryIndex = 0;
        int registered = 0;
        while (entryIndex < entryCount)
        {
            int entryInSector = entryIndex % GptEntriesPerSector;
            if (entryInSector == 0)
            {
                rc = device.ReadSectors(entryLba + (long)sectorIndex, 1, entries, 0);
                if (rc < 0)
                    return rc;
                if (rc != 1)
                    return StorageStatus.IoError;
                sectorIndex++;
            }

            int entryOffset = entryInSector * GptEntrySize;
            if (!IsEntryEmpty(entries, entryOffset))
            {
                long startLba = GetQword(entries, entryOffset + 32);
                long endLba = GetQword(entries, entryOffset + 40);
                if (startLba >= 0 && endLba >= startLba)
                {
                    PartitionInfo partition = new PartitionInfo();
                    string name = string.Concat(info.Name, IntToString(entryIndex + 1));
                    string label = ReadGptName(entries, entryOffset + 56);
                    partition.Initialize(name, label, info.Name, entryIndex + 1, startLba, endLba, 2);
                    int reg = PartitionRegistry.Register(partition);
                    if (reg == StorageStatus.Ok || reg == StorageStatus.AlreadyExists)
                        registered++;
                }
            }

            entryIndex++;
        }

        return registered;
    }

    private static bool IsEntryEmpty(byte[] buffer, int offset)
    {
        for (int i = 0; i < 16; i++)
        {
            if (buffer[offset + i] != 0)
                return false;
        }

        return true;
    }

    private static string ReadGptName(byte[] buffer, int offset)
    {
        string value = "";
        int i = 0;
        while (i < 36)
        {
            int ch = buffer[offset + i * 2] & 0xFF;
            int hi = buffer[offset + i * 2 + 1] & 0xFF;
            if (ch == 0 && hi == 0)
                return value;

            value = string.Concat(value, ByteToString(ch));
            i++;
        }

        return value;
    }

    private static int GetDword(byte[] buffer, int offset)
    {
        return (buffer[offset] & 0xFF)
            | ((buffer[offset + 1] & 0xFF) << 8)
            | ((buffer[offset + 2] & 0xFF) << 16)
            | ((buffer[offset + 3] & 0xFF) << 24);
    }

    private static long GetQword(byte[] buffer, int offset)
    {
        long lo = (long)GetDword(buffer, offset) & 0xFFFFFFFFL;
        long hi = (long)GetDword(buffer, offset + 4) & 0xFFFFFFFFL;
        return lo | (hi << 32);
    }

    private static string IntToString(int value)
    {
        if (value == 0) return "0";
        if (value == 1) return "1";
        if (value == 2) return "2";
        if (value == 3) return "3";
        if (value == 4) return "4";
        if (value == 5) return "5";
        if (value == 6) return "6";
        if (value == 7) return "7";
        if (value == 8) return "8";
        if (value == 9) return "9";
        if (value == 10) return "10";
        if (value == 11) return "11";
        if (value == 12) return "12";
        if (value == 13) return "13";
        if (value == 14) return "14";
        if (value == 15) return "15";
        if (value == 16) return "16";
        return "n";
    }

    private static string ByteToString(int value)
    {
        if (value == 0x30) return "0";
        if (value == 0x31) return "1";
        if (value == 0x32) return "2";
        if (value == 0x33) return "3";
        if (value == 0x34) return "4";
        if (value == 0x35) return "5";
        if (value == 0x36) return "6";
        if (value == 0x37) return "7";
        if (value == 0x38) return "8";
        if (value == 0x39) return "9";
        if (value == 0x41) return "A";
        if (value == 0x42) return "B";
        if (value == 0x43) return "C";
        if (value == 0x44) return "D";
        if (value == 0x45) return "E";
        if (value == 0x46) return "F";
        if (value == 0x47) return "G";
        if (value == 0x48) return "H";
        if (value == 0x49) return "I";
        if (value == 0x4A) return "J";
        if (value == 0x4B) return "K";
        if (value == 0x4C) return "L";
        if (value == 0x4D) return "M";
        if (value == 0x4E) return "N";
        if (value == 0x4F) return "O";
        if (value == 0x50) return "P";
        if (value == 0x51) return "Q";
        if (value == 0x52) return "R";
        if (value == 0x53) return "S";
        if (value == 0x54) return "T";
        if (value == 0x55) return "U";
        if (value == 0x56) return "V";
        if (value == 0x57) return "W";
        if (value == 0x58) return "X";
        if (value == 0x59) return "Y";
        if (value == 0x5A) return "Z";
        if (value == 0x5F) return "_";
        if (value == 0x2D) return "-";
        return "?";
    }
}
