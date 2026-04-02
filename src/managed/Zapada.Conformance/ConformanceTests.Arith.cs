/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.Arith.cs
 *
 * Arithmetic and conversion opcode conformance tests (I4).
 *
 * Adapted from:
 *   dotnet/runtime/src/tests/JIT/IL_Conformance/Old/Conformance_Base/
 *     shr_u4.il, div_u4.il, rem_u4.il, Conv_I4.il, Conv_I8.il, ConvDLL.il,
 *     ceq_i4.il, cgt_i4.il, clt_i4.il, cgt_u4.il, clt_u4.il,
 *     div_i4.il, rem_i4.il, nop_Conformance.il
 */

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    /*
     * TestShrUn - adapted from shr_u4.il
     *
     * shr.un must not sign-extend: shifting 0xAAAAAAAA right by 1 gives
     * 0x55555555 (not 0xD5555555 which signed shr would give).
     *
     * Verified with two cases:
     *   0xFFFFFFFF >> 1 = 0x7FFFFFFF   (top bit cleared, no sign extension)
     *   0xAAAAAAAA >> 1 = 0x55555555   (equals s_odd)
     */
    private static void TestShrUn()
    {
        uint all  = s_all;
        uint even = s_even;

        /* 0xFFFFFFFF shr.un 1 = 0x7FFFFFFF */
        uint v1 = all >> 1;
        if (v1 == 0x7FFFFFFFu) Pass("[PASS] shr.un 0xFFFFFFFF>>1=0x7FFFFFFF"); else Fail("[FAIL] shr.un 0xFFFFFFFF>>1");

        /* 0xAAAAAAAA shr.un 1 = 0x55555555 */
        uint v2 = even >> 1;
        if (v2 == s_odd) Pass("[PASS] shr.un 0xAAAAAAAA>>1=0x55555555"); else Fail("[FAIL] shr.un 0xAAAAAAAA>>1");
    }

    /*
     * TestDivRemUn - adapted from div_u4.il / rem_u4.il conformance tests
     *
     *   7u / 2u = 3
     *   7u % 2u = 1
     *   0xFFFFFFFF / 2u = 0x7FFFFFFF
     */
    private static void TestDivRemUn()
    {
        uint seven = 7u;
        uint two   = 2u;

        /* 7u div.un 2u = 3 */
        uint q1 = seven / two;
        if (q1 == 3u) Pass("[PASS] div.un 7/2=3"); else Fail("[FAIL] div.un 7/2");

        /* 7u rem.un 2u = 1 */
        uint r1 = seven % two;
        if (r1 == 1u) Pass("[PASS] rem.un 7%2=1"); else Fail("[FAIL] rem.un 7%2");

        /* 0xFFFFFFFF div.un 2u = 0x7FFFFFFF */
        uint q2 = s_all / two;
        if (q2 == 0x7FFFFFFFu) Pass("[PASS] div.un 0xFFFFFFFF/2=0x7FFFFFFF"); else Fail("[FAIL] div.un 0xFFFFFFFF/2");
    }

    /*
     * TestConvOps - adapted from Conv_I4.il, Conv_I8.il, ConvDLL.il
     *
     * conv.i1:  (sbyte)0x80 = -128; (sbyte)127 = 127
     * conv.u1:  (byte)0x80  = 128; (byte)(-1) = 255
     * conv.i2:  (short)0x8000 = -32768
     * conv.u2:  (ushort)0x8000 = 32768
     * conv.i4:  (int)(long)42 = 42
     * conv.u4:  (uint)(int)(-1) = 0xFFFFFFFF = 4294967295
     * conv.i8:  (long)42 = 42L
     * conv.u8:  (ulong)(uint)0xFFFFFFFF = 4294967295L
     */
    private static void TestConvOps()
    {
        /* conv.i1: truncate to 8 bits, sign-extend */
        int rawI1Neg = 0x80;             /* 128 as int, will be -128 as sbyte */
        int convI1Neg = (int)(sbyte)rawI1Neg;
        if (convI1Neg == -128) Pass("[PASS] conv.i1 0x80=-128"); else Fail("[FAIL] conv.i1 0x80");

        int rawI1Pos = 127;
        int convI1Pos = (int)(sbyte)rawI1Pos;
        if (convI1Pos == 127) Pass("[PASS] conv.i1 127=127"); else Fail("[FAIL] conv.i1 127");

        /* conv.u1: truncate to 8 bits, zero-extend */
        int rawU1 = 0x180;               /* 384; low byte = 0x80 = 128 */
        int convU1 = (int)(byte)rawU1;
        if (convU1 == 128) Pass("[PASS] conv.u1 0x180=128"); else Fail("[FAIL] conv.u1 0x180");

        int rawU1Neg = -1;               /* 0xFFFFFFFF; low byte = 0xFF = 255 */
        int convU1Neg = (int)(byte)rawU1Neg;
        if (convU1Neg == 255) Pass("[PASS] conv.u1 -1=255"); else Fail("[FAIL] conv.u1 -1");

        /* conv.i2: truncate to 16 bits, sign-extend */
        int rawI2 = 0x8000;              /* 32768; as short = -32768 */
        int convI2 = (int)(short)rawI2;
        if (convI2 == -32768) Pass("[PASS] conv.i2 0x8000=-32768"); else Fail("[FAIL] conv.i2 0x8000");

        /* conv.u2: truncate to 16 bits, zero-extend */
        int rawU2 = 0x18000;             /* low 16 bits = 0x8000 = 32768 */
        int convU2 = (int)(ushort)rawU2;
        if (convU2 == 32768) Pass("[PASS] conv.u2 0x18000=32768"); else Fail("[FAIL] conv.u2 0x18000");

        /* conv.i4: truncate I8 to I4 */
        long rawI4 = 42L;
        int convI4 = (int)rawI4;
        if (convI4 == 42) Pass("[PASS] conv.i4 42L=42"); else Fail("[FAIL] conv.i4 42L");

        /* conv.i4: truncate negative long, low 32 bits */
        long rawI4Neg = -1L;
        int convI4Neg = (int)rawI4Neg;
        if (convI4Neg == -1) Pass("[PASS] conv.i4 -1L=-1"); else Fail("[FAIL] conv.i4 -1L");

        /* conv.u4: reinterpret -1 (I4) as uint = 4294967295 */
        int negOne = -1;
        uint convU4 = (uint)negOne;
        if (convU4 == 4294967295u) Pass("[PASS] conv.u4 -1=4294967295"); else Fail("[FAIL] conv.u4 -1");

        /* conv.i8: widen I4 to I8 */
        int rawI8 = 42;
        long convI8 = (long)rawI8;
        if (convI8 == 42L) Pass("[PASS] conv.i8 42=42L"); else Fail("[FAIL] conv.i8 42");

        /* conv.i8 negative: -42 widened to -42L */
        int rawI8Neg = -42;
        long convI8Neg = (long)rawI8Neg;
        if (convI8Neg == -42L) Pass("[PASS] conv.i8 -42=-42L"); else Fail("[FAIL] conv.i8 -42");

        /* conv.u8: zero-extend uint 0xFFFFFFFF to I8; compare via signed I8 interpretation */
        /* 0xFFFFFFFF as ulong = 4294967295; as long = 4294967295L (fits in positive I8 range) */
        uint rawU8 = 0xFFFFFFFFu;
        long convU8 = (long)(ulong)rawU8;   /* conv.u4 then conv.u8: zero-extends to 0x00000000FFFFFFFF */
        if (convU8 == 4294967295L) Pass("[PASS] conv.u8 0xFFFFFFFF=4294967295L"); else Fail("[FAIL] conv.u8 0xFFFFFFFF");
    }

    /*
     * TestCmpOps - adapted from ceq_i4.il, cgt_i4.il, clt_i4.il, cgt_u4.il, clt_u4.il
     *
     * Tests ceq, cgt, clt, cgt.un, clt.un as value-producing comparison instructions.
     * In C# these appear when storing comparisons into bool locals:
     *   bool b = (a == b);   =>  ceq; box or brtrue pattern depending on context
     *   bool b = (a > b);    =>  cgt
     *   bool b = (a < b);    =>  clt
     * For unsigned: (uint a > uint b) =>  cgt.un
     *               (uint a < uint b) =>  clt.un
     */
    private static void TestCmpOps()
    {
        /* ceq: 5 == 5 -> true; 5 == 6 -> false */
        int five  = 5;
        int six   = 6;
#pragma warning disable CS1718  // intentional: ceq self-comparison is the test target
        bool ceqTrue  = (five == five);
#pragma warning restore CS1718
        bool ceqFalse = (five == six);
        if (ceqTrue)  Pass("[PASS] ceq i4 5==5 true");  else Fail("[FAIL] ceq i4 5==5");
        if (!ceqFalse) Pass("[PASS] ceq i4 5==6 false"); else Fail("[FAIL] ceq i4 5==6");

        /* cgt: 6 > 5 -> true; -1 > 0 -> false (signed) */
        bool cgtTrue  = (six > five);
        bool cgtFalse = (-1 > 0);
        if (cgtTrue)  Pass("[PASS] cgt i4 6>5 true");  else Fail("[FAIL] cgt i4 6>5");
        if (!cgtFalse) Pass("[PASS] cgt i4 -1>0 false"); else Fail("[FAIL] cgt i4 -1>0");

        /* clt: 5 < 6 -> true; -1 < 0 -> true (signed) */
        bool cltTrue  = (five < six);
        bool cltTrue2 = (-1 < 0);
        if (cltTrue)  Pass("[PASS] clt i4 5<6 true");  else Fail("[FAIL] clt i4 5<6");
        if (cltTrue2) Pass("[PASS] clt i4 -1<0 true"); else Fail("[FAIL] clt i4 -1<0");

        /* cgt.un: uint 0xFFFFFFFF > uint 0 -> true; uint 0 > uint 1 -> false */
        uint uall  = s_all;   /* 0xFFFFFFFF */
        uint unone = s_none;  /* 0x00000000 */
        bool cgtUnTrue  = (uall > unone);
        bool cgtUnFalse = (unone > uall);
        if (cgtUnTrue)  Pass("[PASS] cgt.un u4 all>none true");  else Fail("[FAIL] cgt.un u4 all>none");
        if (!cgtUnFalse) Pass("[PASS] cgt.un u4 none>all false"); else Fail("[FAIL] cgt.un u4 none>all");

        /* clt.un: uint 0 < uint 0xFFFFFFFF -> true; uint 0xFFFFFFFF < uint 0 -> false */
        bool cltUnTrue  = (unone < uall);
        bool cltUnFalse = (uall < unone);
        if (cltUnTrue)  Pass("[PASS] clt.un u4 none<all true");  else Fail("[FAIL] clt.un u4 none<all");
        if (!cltUnFalse) Pass("[PASS] clt.un u4 all<none false"); else Fail("[FAIL] clt.un u4 all<none");
    }

    /*
     * TestSignedDivRem - adapted from div_i4.il, rem_i4.il
     *
     * Signed integer division and remainder with boundary cases.
     * These are separate from div.un/rem.un which operate on unsigned values.
     *
     *   7 / 2   =  3    (truncates toward zero)
     *   7 % 2   =  1
     *  -7 / 2   = -3    (truncates toward zero)
     *  -7 % 2   = -1
     *   7 / -2  = -3
     *   7 % -2  =  1
     *  -1 / 1   = -1
     */
    private static void TestSignedDivRem()
    {
        int seven    = 7;
        int negSeven = -7;
        int two      = 2;
        int negTwo   = -2;

        /* 7 / 2 = 3 */
        int q1 = seven / two;
        if (q1 == 3) Pass("[PASS] div.i4 7/2=3"); else Fail("[FAIL] div.i4 7/2");

        /* 7 % 2 = 1 */
        int r1 = seven % two;
        if (r1 == 1) Pass("[PASS] rem.i4 7%2=1"); else Fail("[FAIL] rem.i4 7%2");

        /* -7 / 2 = -3 (truncates toward zero) */
        int q2 = negSeven / two;
        if (q2 == -3) Pass("[PASS] div.i4 -7/2=-3"); else Fail("[FAIL] div.i4 -7/2");

        /* -7 % 2 = -1 (sign follows dividend) */
        int r2 = negSeven % two;
        if (r2 == -1) Pass("[PASS] rem.i4 -7%2=-1"); else Fail("[FAIL] rem.i4 -7%2");

        /* 7 / -2 = -3 */
        int q3 = seven / negTwo;
        if (q3 == -3) Pass("[PASS] div.i4 7/-2=-3"); else Fail("[FAIL] div.i4 7/-2");

        /* 7 % -2 = 1 (sign follows dividend) */
        int r3 = seven % negTwo;
        if (r3 == 1) Pass("[PASS] rem.i4 7%-2=1"); else Fail("[FAIL] rem.i4 7%-2");
    }

    /*
     * TestNop - adapted from nop_Conformance.il
     *
     * The nop instruction must have no observable effect on the evaluation
     * stack or local variables.  We test by inserting explicit no-ops via
     * a call that does nothing and verifying surrounding state is unchanged.
     *
     * The C# compiler emits nop in debug builds at statement boundaries.
     * In release builds we can still exercise it by using patterns that
     * preserve it, e.g. empty statement in a loop.
     */
    private static void TestCheckedArithmetic()
    {
        TestCheckedArithmeticCore();
        TestCheckedConversionsCore();
        TestCheckedConversionsUnsignedCore();
        TestCheckedNativeIntCore();
        TestCheckedOverflowEdgeCore();
    }

    private static void TestCheckedArithmeticCore()
    {
        try
        {
            int max = int.MaxValue;
            int one = 1;
            int value = checked(max + one);
            if (value == 0)
                Fail("[FAIL] add.ovf int32 throws");
            else
                Fail("[FAIL] add.ovf int32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] add.ovf int32 throws");
        }

        try
        {
            uint all = 0xFFFFFFFFu;
            uint one = 1u;
            uint value = checked(all + one);
            if (value == 0u)
                Fail("[FAIL] add.ovf.un uint32 throws");
            else
                Fail("[FAIL] add.ovf.un uint32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] add.ovf.un uint32 throws");
        }

        try
        {
            int min = int.MinValue;
            int one = 1;
            int value = checked(min - one);
            if (value == 0)
                Fail("[FAIL] sub.ovf int32 throws");
            else
                Fail("[FAIL] sub.ovf int32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] sub.ovf int32 throws");
        }

        try
        {
            int left = 50000;
            int right = 50000;
            int value = checked(left * right);
            if (value == 0)
                Fail("[FAIL] mul.ovf int32 throws");
            else
                Fail("[FAIL] mul.ovf int32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] mul.ovf int32 throws");
        }
    }

    private static void TestCheckedConversionsCore()
    {

        try
        {
            int neg = -1;
            byte value = checked((byte)neg);
            if (value == 0)
                Fail("[FAIL] conv.ovf.u1 int32 throws");
            else
                Fail("[FAIL] conv.ovf.u1 int32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.u1 int32 throws");
        }

        try
        {
            int big = 128;
            sbyte value = checked((sbyte)big);
            if (value == 0)
                Fail("[FAIL] conv.ovf.i1 int32 throws");
            else
                Fail("[FAIL] conv.ovf.i1 int32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.i1 int32 throws");
        }

        try
        {
            int neg = -1;
            ushort value = checked((ushort)neg);
            if (value == 0)
                Fail("[FAIL] conv.ovf.u2 int32 throws");
            else
                Fail("[FAIL] conv.ovf.u2 int32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.u2 int32 throws");
        }

        try
        {
            long big = 2147483648L;
            int value = checked((int)big);
            if (value == 0)
                Fail("[FAIL] conv.ovf.i4 int64 throws");
            else
                Fail("[FAIL] conv.ovf.i4 int64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.i4 int64 throws");
        }

        try
        {
            int big = 32768;
            short value = checked((short)big);
            if (value == 0)
                Fail("[FAIL] conv.ovf.i2 int32 throws");
            else
                Fail("[FAIL] conv.ovf.i2 int32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.i2 int32 throws");
        }

        try
        {
            long big = 4294967296L;
            uint value = checked((uint)big);
            if (value == 0u)
                Fail("[FAIL] conv.ovf.u4 int64 throws");
            else
                Fail("[FAIL] conv.ovf.u4 int64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.u4 int64 throws");
        }

        try
        {
            ulong big = 9223372036854775808UL;
            long value = checked((long)big);
            if (value == 0L)
                Fail("[FAIL] conv.ovf.i8.un uint64 throws");
            else
                Fail("[FAIL] conv.ovf.i8.un uint64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.i8.un uint64 throws");
        }

        try
        {
            long neg = -1L;
            ulong value = checked((ulong)neg);
            if (value == 0UL)
                Fail("[FAIL] conv.ovf.u8 int64 throws");
            else
                Fail("[FAIL] conv.ovf.u8 int64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.u8 int64 throws");
        }
    }

    private static void TestCheckedConversionsUnsignedCore()
    {

        try
        {
            uint all = 0xFFFFFFFFu;
            short value = checked((short)all);
            if (value == 0)
                Fail("[FAIL] conv.ovf.i2.un uint32 throws");
            else
                Fail("[FAIL] conv.ovf.i2.un uint32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.i2.un uint32 throws");
        }

        try
        {
            uint all = 0xFFFFFFFFu;
            byte value = checked((byte)all);
            if (value == 0)
                Fail("[FAIL] conv.ovf.u1.un uint32 throws");
            else
                Fail("[FAIL] conv.ovf.u1.un uint32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.u1.un uint32 throws");
        }

        try
        {
            ulong all = 0xFFFFFFFFFFFFFFFFUL;
            uint value = checked((uint)all);
            if (value == 0u)
                Fail("[FAIL] conv.ovf.u4.un uint64 throws");
            else
                Fail("[FAIL] conv.ovf.u4.un uint64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.u4.un uint64 throws");
        }

        try
        {
            ulong all = 0xFFFFFFFFFFFFFFFFUL;
            int value = checked((int)all);
            if (value == 0)
                Fail("[FAIL] conv.ovf.i4.un uint64 throws");
            else
                Fail("[FAIL] conv.ovf.i4.un uint64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.i4.un uint64 throws");
        }

        try
        {
            uint all = 0xFFFFFFFFu;
            long value = checked((long)all);
            if (value == 4294967295L)
                Pass("[PASS] conv.ovf.i8.un uint32 succeeds");
            else
                Fail("[FAIL] conv.ovf.i8.un uint32 succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.i8.un uint32 succeeds");
        }

        try
        {
            uint all = 0xFFFFFFFFu;
            ulong value = checked((ulong)all);
            if (value == 4294967295UL)
                Pass("[PASS] conv.ovf.u8.un uint32 succeeds");
            else
                Fail("[FAIL] conv.ovf.u8.un uint32 succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.u8.un uint32 succeeds");
        }

        try
        {
            int pos = 123;
            nint value = checked((nint)pos);
            if (value == 123)
                Pass("[PASS] conv.ovf.i native int32 succeeds");
            else
                Fail("[FAIL] conv.ovf.i native int32 succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.i native int32 succeeds");
        }

        try
        {
            uint pos = 123u;
            nuint value = checked((nuint)pos);
            if (value == 123u)
                Pass("[PASS] conv.ovf.u.un native uint32 succeeds");
            else
                Fail("[FAIL] conv.ovf.u.un native uint32 succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.u.un native uint32 succeeds");
        }

        try
        {
            ulong pos = 123UL;
            nint value = checked((nint)pos);
            if (value == 123)
                Pass("[PASS] conv.ovf.i.un native uint64 small succeeds");
            else
                Fail("[FAIL] conv.ovf.i.un native uint64 small succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.i.un native uint64 small succeeds");
        }
    }

    private static void TestCheckedNativeIntCore()
    {

        try
        {
            ulong big = 9223372036854775808UL;
            nint value = checked((nint)big);
            if (value == 0)
                Fail("[FAIL] conv.ovf.i.un native uint64 throws");
            else
                Fail("[FAIL] conv.ovf.i.un native uint64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.i.un native uint64 throws");
        }

        try
        {
            ulong pos = 123UL;
            nuint value = checked((nuint)pos);
            if (value == 123UL)
                Pass("[PASS] conv.ovf.u.un native uint64 succeeds");
            else
                Fail("[FAIL] conv.ovf.u.un native uint64 succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.u.un native uint64 succeeds");
        }

        try
        {
            uint all = 0xFFFFFFFFu;
            nint value = checked((nint)all);
            if ((long)value == 4294967295L)
                Pass("[PASS] conv.ovf.i.un native uint32 max succeeds");
            else
                Fail("[FAIL] conv.ovf.i.un native uint32 max succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.i.un native uint32 max succeeds");
        }

        try
        {
            int neg = -1;
            uint value = checked((uint)neg);
            if (value == 0u)
                Fail("[FAIL] conv.ovf.u4 int32 negative throws");
            else
                Fail("[FAIL] conv.ovf.u4 int32 negative throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.u4 int32 negative throws");
        }

        try
        {
            long min = long.MinValue;
            long value = checked(-min);
            if (value == 0L)
                Fail("[FAIL] neg overflow int64 throws");
            else
                Fail("[FAIL] neg overflow int64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] neg overflow int64 throws");
        }

        try
        {
            int min = int.MinValue;
            int value = checked(-min);
            if (value == 0)
                Fail("[FAIL] neg overflow int32 throws");
            else
                Fail("[FAIL] neg overflow int32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] neg overflow int32 throws");
        }
    }

    private static void TestCheckedOverflowEdgeCore()
    {

        try
        {
            int value = checked((int)(sbyte)(-1));
            if (value == -1)
                Pass("[PASS] conv.ovf.i1 int32 negative succeeds");
            else
                Fail("[FAIL] conv.ovf.i1 int32 negative succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.i1 int32 negative succeeds");
        }

        try
        {
            int value = checked((int)(byte)255);
            if (value == 255)
                Pass("[PASS] conv.ovf.u1 int32 255 succeeds");
            else
                Fail("[FAIL] conv.ovf.u1 int32 255 succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.u1 int32 255 succeeds");
        }

        try
        {
            int value = checked((int)(short)(-32768));
            if (value == -32768)
                Pass("[PASS] conv.ovf.i2 int32 min succeeds");
            else
                Fail("[FAIL] conv.ovf.i2 int32 min succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.i2 int32 min succeeds");
        }

        try
        {
            int value = checked((int)(ushort)65535);
            if (value == 65535)
                Pass("[PASS] conv.ovf.u2 int32 max succeeds");
            else
                Fail("[FAIL] conv.ovf.u2 int32 max succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.u2 int32 max succeeds");
        }

        try
        {
            uint value = checked((uint)1);
            if (value == 1u)
                Pass("[PASS] conv.ovf.u4 int32 positive succeeds");
            else
                Fail("[FAIL] conv.ovf.u4 int32 positive succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.u4 int32 positive succeeds");
        }

        try
        {
            long value = checked((long)9223372036854775807UL);
            if (value == long.MaxValue)
                Pass("[PASS] conv.ovf.i8.un uint64 max-signed succeeds");
            else
                Fail("[FAIL] conv.ovf.i8.un uint64 max-signed succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.i8.un uint64 max-signed succeeds");
        }

        try
        {
            ulong value = checked((ulong)9223372036854775807L);
            if (value == 9223372036854775807UL)
                Pass("[PASS] conv.ovf.u8 int64 positive succeeds");
            else
                Fail("[FAIL] conv.ovf.u8 int64 positive succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.u8 int64 positive succeeds");
        }

        try
        {
            ulong value = checked((ulong)ulong.MaxValue);
            if (value == ulong.MaxValue)
                Pass("[PASS] conv.ovf.u8.un uint64 max succeeds");
            else
                Fail("[FAIL] conv.ovf.u8.un uint64 max succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.u8.un uint64 max succeeds");
        }

        try
        {
            uint value = checked((uint)255u);
            if (value == 255u)
                Pass("[PASS] conv.ovf.u1.un uint32 255 succeeds");
            else
                Fail("[FAIL] conv.ovf.u1.un uint32 255 succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.u1.un uint32 255 succeeds");
        }

        try
        {
            uint value = checked((uint)65535u);
            if (value == 65535u)
                Pass("[PASS] conv.ovf.u2.un uint32 max succeeds");
            else
                Fail("[FAIL] conv.ovf.u2.un uint32 max succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.u2.un uint32 max succeeds");
        }

        try
        {
            uint value = 127u;
            int signed = checked((sbyte)value);
            if (signed == 127)
                Pass("[PASS] conv.ovf.i1.un uint32 127 succeeds");
            else
                Fail("[FAIL] conv.ovf.i1.un uint32 127 succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.i1.un uint32 127 succeeds");
        }

        try
        {
            uint value = 32767u;
            int signed = checked((short)value);
            if (signed == 32767)
                Pass("[PASS] conv.ovf.i2.un uint32 max-signed succeeds");
            else
                Fail("[FAIL] conv.ovf.i2.un uint32 max-signed succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.i2.un uint32 max-signed succeeds");
        }

        try
        {
            uint value = 2147483647u;
            int signed = checked((int)value);
            if (signed == 2147483647)
                Pass("[PASS] conv.ovf.i4.un uint32 max-signed succeeds");
            else
                Fail("[FAIL] conv.ovf.i4.un uint32 max-signed succeeds");
        }
        catch (global::System.Exception)
        {
            Fail("[FAIL] conv.ovf.i4.un uint32 max-signed succeeds");
        }

        try
        {
            uint value = 2147483648u;
            int signed = checked((int)value);
            if (signed == 0)
                Fail("[FAIL] conv.ovf.i4.un uint32 throws at high-bit");
            else
                Fail("[FAIL] conv.ovf.i4.un uint32 throws at high-bit");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] conv.ovf.i4.un uint32 throws at high-bit");
        }

        try
        {
            uint left = 0xFFFFFFFFu;
            uint right = 2u;
            uint value = checked(left * right);
            if (value == 0u)
                Fail("[FAIL] mul.ovf.un uint32 throws");
            else
                Fail("[FAIL] mul.ovf.un uint32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] mul.ovf.un uint32 throws");
        }

        try
        {
            uint zero = 0u;
            uint one = 1u;
            uint value = checked(zero - one);
            if (value == 0u)
                Fail("[FAIL] sub.ovf.un uint32 throws");
            else
                Fail("[FAIL] sub.ovf.un uint32 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] sub.ovf.un uint32 throws");
        }

        try
        {
            long max = long.MaxValue;
            long one = 1L;
            long value = checked(max + one);
            if (value == 0L)
                Fail("[FAIL] add.ovf int64 throws");
            else
                Fail("[FAIL] add.ovf int64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] add.ovf int64 throws");
        }

        try
        {
            ulong max = ulong.MaxValue;
            ulong one = 1UL;
            ulong value = checked(max + one);
            if (value == 0UL)
                Fail("[FAIL] add.ovf.un uint64 throws");
            else
                Fail("[FAIL] add.ovf.un uint64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] add.ovf.un uint64 throws");
        }

        try
        {
            long min = long.MinValue;
            long one = 1L;
            long value = checked(min - one);
            if (value == 0L)
                Fail("[FAIL] sub.ovf int64 throws");
            else
                Fail("[FAIL] sub.ovf int64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] sub.ovf int64 throws");
        }

        try
        {
            ulong zero = 0UL;
            ulong one = 1UL;
            ulong value = checked(zero - one);
            if (value == 0UL)
                Fail("[FAIL] sub.ovf.un uint64 throws");
            else
                Fail("[FAIL] sub.ovf.un uint64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] sub.ovf.un uint64 throws");
        }

        try
        {
            long left = 3037000500L;
            long right = 3037000500L;
            long value = checked(left * right);
            if (value == 0L)
                Fail("[FAIL] mul.ovf int64 throws");
            else
                Fail("[FAIL] mul.ovf int64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] mul.ovf int64 throws");
        }

        try
        {
            ulong left = ulong.MaxValue;
            ulong right = 2UL;
            ulong value = checked(left * right);
            if (value == 0UL)
                Fail("[FAIL] mul.ovf.un uint64 throws");
            else
                Fail("[FAIL] mul.ovf.un uint64 throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] mul.ovf.un uint64 throws");
        }
    }

    private static void TestNop()
    {
        int before = 42;
        /* The C# compiler (debug) emits nop before this assignment */
        int after = before;
        if (after == 42) Pass("[PASS] nop no-effect i4"); else Fail("[FAIL] nop no-effect i4");

        /* Verify a loop body with an empty iteration (nop at loop end) */
        int count = 0;
        int i = 0;
        while (i < 3)
        {
            count = count + 1;
            i = i + 1;
        }
        if (count == 3) Pass("[PASS] nop loop body intact"); else Fail("[FAIL] nop loop body intact");
    }

    private static void TestFloatArithmetic()
    {
        TestFloatArithmeticCore();
        TestFloatArrayCore();
        TestFloatConversionsCore();
    }

    private static void TestFloatArithmeticCore()
    {
        float a = 1.5f;
        float b = 2.25f;
        float sum = a + b;
        float product = a * b;
        double quotient = 9.0 / 2.0;
        double remainder = 9.0 % 2.0;

        if (sum > 3.74f && sum < 3.76f)
            Pass("[PASS] float add r4");
        else
            Fail("[FAIL] float add r4");

        if (product > 3.37f && product < 3.38f)
            Pass("[PASS] float mul r4");
        else
            Fail("[FAIL] float mul r4");

        if (quotient > 4.49 && quotient < 4.51)
            Pass("[PASS] float div r8");
        else
            Fail("[FAIL] float div r8");

        if (remainder > 0.99 && remainder < 1.01)
            Pass("[PASS] float rem r8");
        else
            Fail("[FAIL] float rem r8");
    }

    private static void TestFloatArrayCore()
    {
        float[] singles = new float[2];
        double[] doubles = new double[2];

        singles[0] = 1.25f;
        singles[1] = 2.5f;
        doubles[0] = 4.5;
        doubles[1] = 8.25;

        if (singles[0] > 1.24f && singles[0] < 1.26f && singles[1] > 2.49f && singles[1] < 2.51f)
            Pass("[PASS] float array r4 load/store");
        else
            Fail("[FAIL] float array r4 load/store");

        if (doubles[0] > 4.49 && doubles[0] < 4.51 && doubles[1] > 8.24 && doubles[1] < 8.26)
            Pass("[PASS] float array r8 load/store");
        else
            Fail("[FAIL] float array r8 load/store");
    }

    private static void TestFloatConversionsCore()
    {
        float fromInt = (float)5;
        double fromLong = (double)7L;
        uint u = 4000000000u;
        double fromUnsigned = u;

        if (fromInt > 4.99f && fromInt < 5.01f)
            Pass("[PASS] conv.r4 int32");
        else
            Fail("[FAIL] conv.r4 int32");

        if (fromLong > 6.99 && fromLong < 7.01)
            Pass("[PASS] conv.r8 int64");
        else
            Fail("[FAIL] conv.r8 int64");

        if (fromUnsigned > 3999999999.0 && fromUnsigned < 4000000001.0)
            Pass("[PASS] conv.r.un uint32");
        else
            Fail("[FAIL] conv.r.un uint32");
    }
}


