/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.I8.cs
 *
 * Int64 (I8) arithmetic, bitwise, shift, and branch conformance tests.
 *
 * Adapted from:
 *   dotnet/runtime/src/tests/JIT/IL_Conformance/Old/Conformance_Base/
 *     add_i8.il, sub_i8.il, mul_i8.il, div_i8.il, rem_i8.il,
 *     neg_i8.il, and_u8.il, or_u8.il, xor_u8.il, not_u8.il,
 *     shl_u8.il, shr_i8.il, shr_u8.il,
 *     bge_i8.il, bgt_i8.il, ble_i8.il, blt_i8.il,
 *     bge_un_i8.il, bgt_u8.il, ble_u8.il, blt_u8.il, bne_u8.il, beq_i8.il,
 *     cgt_u8.il, clt_u8.il
 *
 * All tests use long / ulong literals and static fields to prevent
 * constant-folding by the C# compiler.
 */

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    /*
     * I8 sentinel values (set during TestI8Arithmetic to avoid extra static fields).
     * Using local variables read from compile-time constants that the interpreter
     * cannot fold (long arithmetic is non-trivial at the CIL level).
     */

    /*
     * TestI8Arithmetic - adapted from add_i8.il, sub_i8.il, mul_i8.il, div_i8.il, rem_i8.il, neg_i8.il
     *
     * Verifies I8 (int64) two-operand arithmetic instructions.
     * Results are compared using signed I8 equality branches (beq.l / ceq on I8).
     */
    private static void TestI8Arithmetic()
    {
        long a = 0x100000000L;   /* 4294967296 */
        long b = 0x200000000L;   /* 8589934592 */

        /* add.i8: 4294967296 + 8589934592 = 12884901888 = 0x300000000 */
        long addResult = a + b;
        if (addResult == 0x300000000L) Pass("[PASS] add.i8 0x1_00000000+0x2_00000000"); else Fail("[FAIL] add.i8");

        /* sub.i8: 8589934592 - 4294967296 = 4294967296 = 0x100000000 */
        long subResult = b - a;
        if (subResult == 0x100000000L) Pass("[PASS] sub.i8 0x2-0x1 = 0x1"); else Fail("[FAIL] sub.i8");

        /* mul.i8: 4294967296 * 2 = 8589934592 */
        long two = 2L;
        long mulResult = a * two;
        if (mulResult == 0x200000000L) Pass("[PASS] mul.i8 0x100000000*2"); else Fail("[FAIL] mul.i8");

        /* div.i8: 8589934592 / 2 = 4294967296 */
        long divResult = b / two;
        if (divResult == 0x100000000L) Pass("[PASS] div.i8 0x200000000/2"); else Fail("[FAIL] div.i8");

        /* rem.i8: 9 % 4 = 1 */
        long nine  = 9L;
        long four  = 4L;
        long remResult = nine % four;
        if (remResult == 1L) Pass("[PASS] rem.i8 9%4=1"); else Fail("[FAIL] rem.i8 9%4");

        /* neg.i8: -(4294967296) = -4294967296 */
        long negResult = -a;
        if (negResult == -0x100000000L) Pass("[PASS] neg.i8 -0x100000000"); else Fail("[FAIL] neg.i8");

        /* signed: -9 / 4 = -2 (truncates toward zero) */
        long negNine = -9L;
        long sdivResult = negNine / four;
        if (sdivResult == -2L) Pass("[PASS] div.i8 -9/4=-2"); else Fail("[FAIL] div.i8 -9/4");

        /* signed: -9 % 4 = -1 (sign follows dividend) */
        long sremResult = negNine % four;
        if (sremResult == -1L) Pass("[PASS] rem.i8 -9%4=-1"); else Fail("[FAIL] rem.i8 -9%4");
    }

    /*
     * TestI8Bitwise - adapted from and_u8.il, or_u8.il, xor_u8.il, not_u8.il
     *
     * Bitwise operations on 64-bit values.
     *
     * and:  0xFFFFFFFF_FFFFFFFF & 0x55555555_55555555 = 0x55555555_55555555
     * or:   0x55555555_55555555 | 0xAAAAAAAA_AAAAAAAA = 0xFFFFFFFF_FFFFFFFF
     * xor:  0xFFFFFFFF_FFFFFFFF ^ 0x55555555_55555555 = 0xAAAAAAAA_AAAAAAAA
     * not:  ~0x55555555_55555555 = 0xAAAAAAAA_AAAAAAAA
     */
    private static void TestI8Bitwise()
    {
        long allI8  = unchecked((long)0xFFFFFFFFFFFFFFFFL);  /* -1L */
        long oddI8  = 0x5555555555555555L;
        long evenI8 = unchecked((long)0xAAAAAAAAAAAAAAAAL);

        /* and */
        long andResult = allI8 & oddI8;
        if (andResult == oddI8) Pass("[PASS] and.u8 all&odd=odd"); else Fail("[FAIL] and.u8 all&odd");

        /* or */
        long orResult = oddI8 | evenI8;
        if (orResult == allI8) Pass("[PASS] or.u8 odd|even=all"); else Fail("[FAIL] or.u8 odd|even");

        /* xor */
        long xorResult = allI8 ^ oddI8;
        if (xorResult == evenI8) Pass("[PASS] xor.u8 all^odd=even"); else Fail("[FAIL] xor.u8 all^odd");

        /* not */
        long notResult = ~oddI8;
        if (notResult == evenI8) Pass("[PASS] not.u8 ~odd=even"); else Fail("[FAIL] not.u8 ~odd");
    }

    /*
     * TestI8Shifts - adapted from shl_u8.il, shr_i8.il, shr_u8.il
     *
     * shl.i8:    0x1 << 32 = 0x100000000 (crosses 32-bit boundary)
     * shr.i8:    -1L >> 1 = -1L (arithmetic, sign-fill)
     * shr.un.u8: 0xFFFFFFFFFFFFFFFFL >> 1 = 0x7FFFFFFFFFFFFFFFL (logical, no sign-fill)
     */
    private static void TestI8Shifts()
    {
        long one = 1L;

        /* shl: 1L << 32 = 0x100000000 */
        long shlResult = one << 32;
        if (shlResult == 0x100000000L) Pass("[PASS] shl.u8 1<<32=0x100000000"); else Fail("[FAIL] shl.u8 1<<32");

        /* shl: 1L << 63 = long.MinValue = 0x8000000000000000 (negative) */
        long shlMin = one << 63;
        if (shlMin == long.MinValue) Pass("[PASS] shl.u8 1<<63=MinValue"); else Fail("[FAIL] shl.u8 1<<63");

        /* shr: -1L >> 1 = -1L (arithmetic right shift fills with sign bit) */
        long negOne = -1L;
        long shrSigned = negOne >> 1;
        if (shrSigned == -1L) Pass("[PASS] shr.i8 -1>>1=-1"); else Fail("[FAIL] shr.i8 -1>>1");

        /* shr.un (via ulong): 0xFFFF...FF >> 1 = 0x7FFF...FF */
        ulong allU8 = 0xFFFFFFFFFFFFFFFFL;
        ulong shrUn = allU8 >> 1;
        if (shrUn == 0x7FFFFFFFFFFFFFFFL) Pass("[PASS] shr.un.u8 all>>1=0x7FFF...FF"); else Fail("[FAIL] shr.un.u8 all>>1");

        /* shr.un: 0x8000000000000000 >> 1 = 0x4000000000000000 (no sign fill) */
        ulong highBit = 0x8000000000000000L;
        ulong shrUnHigh = highBit >> 1;
        if (shrUnHigh == 0x4000000000000000L) Pass("[PASS] shr.un.u8 highbit>>1"); else Fail("[FAIL] shr.un.u8 highbit>>1");
    }

    /*
     * TestI8Branches - adapted from bge_i8.il, bgt_i8.il, ble_i8.il, blt_i8.il,
     *                  bge_un_i8.il, bgt_u8.il, ble_u8.il, blt_u8.il,
     *                  bne_u8.il, beq_i8.il, cgt_u8.il, clt_u8.il
     *
     * Tests signed and unsigned 64-bit branch instructions.
     * These are distinct from the 32-bit variants in that they operate on
     * I8/U8 operands popped from the eval stack.
     */
    private static void TestI8Branches()
    {
        long negL = -1L;
        long zeroL = 0L;
        long posL  = 1L;
        long bigL  = 0x100000000L;

        /* beq.i8: equal long values */
        long sameL = 0L;
        if (zeroL == sameL) Pass("[PASS] beq.i8 0==0"); else Fail("[FAIL] beq.i8 0==0");
        if (negL == zeroL) Fail("[FAIL] beq.i8 -1==0 (expected false)"); else Pass("[PASS] beq.i8 -1!=0");

        /* bne.un.i8 (long != long) */
        if (negL != zeroL) Pass("[PASS] bne.un.i8 -1!=0"); else Fail("[FAIL] bne.un.i8 -1!=0");

        /* bge.i8 signed: -1 >= 0 -> false; 1 >= 0 -> true */
        if (negL >= zeroL) Fail("[FAIL] bge.i8 -1>=0 (expected false)"); else Pass("[PASS] bge.i8 -1!>=0");
        if (posL >= zeroL) Pass("[PASS] bge.i8 1>=0"); else Fail("[FAIL] bge.i8 1>=0");

        /* bgt.i8 signed: -1 > 0 -> false; 0x100000000 > 1 -> true */
        if (negL > zeroL) Fail("[FAIL] bgt.i8 -1>0 (expected false)"); else Pass("[PASS] bgt.i8 -1!>0");
        if (bigL > posL)  Pass("[PASS] bgt.i8 bigL>1"); else Fail("[FAIL] bgt.i8 bigL>1");

        /* ble.i8 signed: -1 <= 0 -> true; 1 <= 0 -> false */
        if (negL <= zeroL) Pass("[PASS] ble.i8 -1<=0"); else Fail("[FAIL] ble.i8 -1<=0");
        if (posL <= zeroL) Fail("[FAIL] ble.i8 1<=0 (expected false)"); else Pass("[PASS] ble.i8 1!<=0");

        /* blt.i8 signed: -1 < 0 -> true; 0 < 0 -> false */
        if (negL < zeroL) Pass("[PASS] blt.i8 -1<0"); else Fail("[FAIL] blt.i8 -1<0");
#pragma warning disable CS1718  // intentional: blt.i8 equal-operands is the test target
        if (zeroL < zeroL) Fail("[FAIL] blt.i8 0<0 (expected false)"); else Pass("[PASS] blt.i8 0!<0");
#pragma warning restore CS1718

        /* unsigned I8 comparisons (bge.un, bgt.un, ble.un, blt.un via ulong) */
        ulong uAllL  = 0xFFFFFFFFFFFFFFFFL;  /* max ulong */
        ulong uNoneL = 0UL;
        ulong uBigL  = 0x100000000UL;

        /* bge.un.u8: max >= 0 -> true; 0 >= max -> false */
        if (uAllL >= uNoneL) Pass("[PASS] bge.un.u8 max>=0"); else Fail("[FAIL] bge.un.u8 max>=0");
        if (uNoneL >= uAllL) Fail("[FAIL] bge.un.u8 0>=max (expected false)"); else Pass("[PASS] bge.un.u8 0!>=max");

        /* bgt.un.u8: max > 0 -> true */
        if (uAllL > uNoneL) Pass("[PASS] bgt.un.u8 max>0"); else Fail("[FAIL] bgt.un.u8 max>0");

        /* ble.un.u8: 0 <= max -> true */
        if (uNoneL <= uAllL) Pass("[PASS] ble.un.u8 0<=max"); else Fail("[FAIL] ble.un.u8 0<=max");

        /* blt.un.u8: 0 < 0x100000000 -> true */
        if (uNoneL < uBigL) Pass("[PASS] blt.un.u8 0<0x100000000"); else Fail("[FAIL] blt.un.u8 0<0x100000000");

        /* cgt.un: 0xFFFFF... > 0 -> true as value-producing comparison */
        bool cgtUnL = (uAllL > uNoneL);
        if (cgtUnL) Pass("[PASS] cgt.un.u8 max>0 true"); else Fail("[FAIL] cgt.un.u8 max>0");

        /* clt.un: 0 < max -> true */
        bool cltUnL = (uNoneL < uAllL);
        if (cltUnL) Pass("[PASS] clt.un.u8 0<max true"); else Fail("[FAIL] clt.un.u8 0<max");
    }
}


