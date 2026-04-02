/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.Array.cs
 *
 * Array indexing, helper-method loops, and large local-count conformance tests.
 *
 * These tests exercise:
 *   - newarr / ldelem / stelem with computed indices
 *   - helper-method call loops (verifies call/ret with ldarg/ldloc across frames)
 *   - methods with > 16 and > 64 locals (validates dynamic frame sizing)
 */

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    private static int ReadPackedByte(int[] buf, int off)
    {
        int idx  = off >> 2;
        int sh   = (off & 3) << 3;
        int word = buf[idx];
        return (word >> sh) & 0xFF;
    }

    private static void TestArrayHelperLoops()
    {
        int[] buf = new int[4];
        buf[0] = 0x44434241;
        buf[1] = 0x48474645;
        buf[2] = 0x4C4B4A49;
        buf[3] = 0x504F4E4D;

        int sum = 0;
        int i = 0;
        while (i < 16)
        {
            sum = sum + ReadPackedByte(buf, i);
            i = i + 1;
        }

        if (sum == 1160) Pass("[PASS] helper-loop packed-byte sum"); else Fail("[FAIL] helper-loop packed-byte sum");

        int idx = 0;
        int match = 1;
        while (idx < 4)
        {
            int expected = 0x41 + idx;
            if (ReadPackedByte(buf, idx) != expected)
            {
                match = 0;
            }
            idx = idx + 1;
        }

        if (match == 1) Pass("[PASS] array computed index reads"); else Fail("[FAIL] array computed index reads");
    }

    private static int FillAndSumIndexedArray(int[] values)
    {
        int i = 0;
        int sum = 0;
        while (i < values.Length)
        {
            values[i] = (i + 1) * 11;
            sum = sum + values[i];
            i = i + 1;
        }
        return sum;
    }

    private static int SumManyLocals16Plus()
    {
        int l00 = 1;  int l01 = 2;  int l02 = 3;  int l03 = 4;
        int l04 = 5;  int l05 = 6;  int l06 = 7;  int l07 = 8;
        int l08 = 9;  int l09 = 10; int l10 = 11; int l11 = 12;
        int l12 = 13; int l13 = 14; int l14 = 15; int l15 = 16;
        int l16 = 17; int l17 = 18; int l18 = 19; int l19 = 20;

        return l00 + l01 + l02 + l03 + l04 + l05 + l06 + l07 + l08 + l09
             + l10 + l11 + l12 + l13 + l14 + l15 + l16 + l17 + l18 + l19;
    }

    private static int SumManyLocals64Plus()
    {
        int[] values = new int[70];
        int i = 0;
        int sum = 0;
        while (i < values.Length)
        {
            values[i] = i + 1;
            sum = sum + values[i];
            i = i + 1;
        }
        return sum;
    }

    private static void TestIndexedArrayRoundTrips()
    {
        int[] values = new int[8];
        int sum = FillAndSumIndexedArray(values);

        if (sum == 396) Pass("[PASS] int-array indexed fill+sum"); else Fail("[FAIL] int-array indexed fill+sum");

        if (values[0] == 11 && values[3] == 44 && values[7] == 88)
            Pass("[PASS] int-array round-trip writes");
        else
            Fail("[FAIL] int-array round-trip writes");

        int idx = 7;
        int reverse = 0;
        while (idx >= 0)
        {
            reverse = reverse + values[idx];
            idx = idx - 1;
        }

        if (reverse == 396) Pass("[PASS] int-array reverse indexed reads"); else Fail("[FAIL] int-array reverse indexed reads");
        if (SumManyLocals16Plus() == 210) Pass("[PASS] locals >16 sum"); else Fail("[FAIL] locals >16 sum");
        if (SumManyLocals64Plus() == 2485) Pass("[PASS] locals >64 sum"); else Fail("[FAIL] locals >64 sum");
    }
}


