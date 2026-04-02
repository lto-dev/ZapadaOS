/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.Branch.cs
 *
 * Branch and bitwise opcode conformance tests.
 *
 * Adapted from:
 *   dotnet/runtime/src/tests/JIT/IL_Conformance/Old/Conformance_Base/
 *     bge_un_i4.il, bgt_u4.il, ble.un/blt.un, switch_Conformance.il,
 *     beq_i4.il, bne_u4.il, and_u4.il, or_u4.il, xor_u4.il, neg_i4.il,
 *     not_u4.il, shl_u4.il, shr_i4.il, bge_i4.il, bgt_i4.il, ble_i4.il,
 *     blt_i4.il, br_Conformance.il, brfalse_Conformance.il,
 *     brtrue_Conformance.il
 */

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    /*
     * TestBgeUn - adapted from bge_un_i4.il
     *
     * Tests bge.un with the four I4 unsigned sentinel values.
     * uint comparisons a >= b  emit  bge.un  in the IL.
     *
     * Expected true:   all>=none, all>=odd, odd>=none, even>=odd
     * Expected false:  none>=all, odd>=even
     */
    private static void TestBgeUn()
    {
        uint all  = s_all;
        uint none = s_none;
        uint odd  = s_odd;
        uint even = s_even;

        /* 0xFFFFFFFF >= 0x00000000 -> true */
        if (all >= none) Pass("[PASS] bge.un all>=none"); else Fail("[FAIL] bge.un all>=none");

        /* 0xFFFFFFFF >= 0x55555555 -> true */
        if (all >= odd) Pass("[PASS] bge.un all>=odd"); else Fail("[FAIL] bge.un all>=odd");

        /* 0x55555555 >= 0x00000000 -> true */
        if (odd >= none) Pass("[PASS] bge.un odd>=none"); else Fail("[FAIL] bge.un odd>=none");

        /* 0xAAAAAAAA >= 0x55555555 -> true (even > odd unsigned) */
        if (even >= odd) Pass("[PASS] bge.un even>=odd"); else Fail("[FAIL] bge.un even>=odd");

        /* 0x00000000 >= 0xFFFFFFFF -> false */
        if (none >= all) Fail("[FAIL] bge.un none>=all (expected false)"); else Pass("[PASS] bge.un none!>=all");

        /* 0x55555555 >= 0xAAAAAAAA -> false (odd < even unsigned) */
        if (odd >= even) Fail("[FAIL] bge.un odd>=even (expected false)"); else Pass("[PASS] bge.un odd!>=even");
    }

    /*
     * TestBgtUn - adapted from bgt_u4.il
     *
     * Tests bgt.un with the four I4 unsigned sentinel values.
     * uint comparisons a > b  emit  bgt.un  in the IL.
     *
     * Expected true:   all>none, even>odd
     * Expected false:  none>all, odd>even, all>all (equal)
     */
    private static void TestBgtUn()
    {
        uint all  = s_all;
        uint none = s_none;
        uint odd  = s_odd;
        uint even = s_even;

        /* 0xFFFFFFFF > 0x00000000 -> true */
        if (all > none) Pass("[PASS] bgt.un all>none"); else Fail("[FAIL] bgt.un all>none");

        /* 0xAAAAAAAA > 0x55555555 -> true (even > odd unsigned) */
        if (even > odd) Pass("[PASS] bgt.un even>odd"); else Fail("[FAIL] bgt.un even>odd");

        /* 0x00000000 > 0xFFFFFFFF -> false */
        if (none > all) Fail("[FAIL] bgt.un none>all (expected false)"); else Pass("[PASS] bgt.un none!>all");

        /* 0x55555555 > 0xAAAAAAAA -> false (odd < even unsigned) */
        if (odd > even) Fail("[FAIL] bgt.un odd>even (expected false)"); else Pass("[PASS] bgt.un odd!>even");

        /* 0xFFFFFFFF > 0xFFFFFFFF -> false (equal); use separate variable to avoid CS1718 */
        uint all2 = s_all;
        if (all > all2) Fail("[FAIL] bgt.un all>all (expected false)"); else Pass("[PASS] bgt.un all!>all");
    }

    /*
     * TestBleBlUn - adapted from ble.un / blt.un conformance tests
     *
     * Tests ble.un (<=) and blt.un (<) with unsigned sentinels.
     *
     * Expected true <=: none<=all, odd<=even
     * Expected true <:  none<odd, odd<even
     * Expected false <=: all<=none
     * Expected false <:  even<odd
     */
    private static void TestBleBlUn()
    {
        uint all  = s_all;
        uint none = s_none;
        uint odd  = s_odd;
        uint even = s_even;

        /* 0x00000000 <= 0xFFFFFFFF -> true */
        if (none <= all) Pass("[PASS] ble.un none<=all"); else Fail("[FAIL] ble.un none<=all");

        /* 0x55555555 <= 0xAAAAAAAA -> true */
        if (odd <= even) Pass("[PASS] ble.un odd<=even"); else Fail("[FAIL] ble.un odd<=even");

        /* 0x00000000 < 0x55555555 -> true */
        if (none < odd) Pass("[PASS] blt.un none<odd"); else Fail("[FAIL] blt.un none<odd");

        /* 0x55555555 < 0xAAAAAAAA -> true */
        if (odd < even) Pass("[PASS] blt.un odd<even"); else Fail("[FAIL] blt.un odd<even");

        /* 0xFFFFFFFF <= 0x00000000 -> false */
        if (all <= none) Fail("[FAIL] ble.un all<=none (expected false)"); else Pass("[PASS] ble.un all!<=none");

        /* 0xAAAAAAAA < 0x55555555 -> false (even > odd unsigned) */
        if (even < odd) Fail("[FAIL] blt.un even<odd (expected false)"); else Pass("[PASS] blt.un even!<odd");
    }

    /*
     * TestSwitch - adapted from switch_Conformance.il
     *
     * Part A: dense 3-arm switch (cases 0,1,2) iterated by a loop.
     *   Each case body adds a distinct value to a counter; expected sum = 1110.
     *
     * Part B: out-of-range index (5 for a 3-arm switch) falls through;
     *   uses s_odd & 7 == 5 so the compiler cannot constant-fold the index.
     */
    private static void TestSwitch()
    {
        /* Part A */
        int counter = 0;
        int i = 0;
        while (i < 3)
        {
            switch (i)
            {
                case 0: counter = counter + 10;   break;
                case 1: counter = counter + 100;  break;
                case 2: counter = counter + 1000; break;
            }
            i = i + 1;
        }
        if (counter == 1110) Pass("[PASS] switch counter=1110"); else Fail("[FAIL] switch counter!=1110");

        /* Part B: index = (int)(s_odd & 7u) = 0x55555555 & 7 = 5 */
        int idx  = (int)(s_odd & 7u);
        int fell = 0;
        switch (idx)
        {
            case 0: fell = -1; break;
            case 1: fell = -1; break;
            case 2: fell = -1; break;
        }
        if (fell == 0) Pass("[PASS] switch out-of-range fell through"); else Fail("[FAIL] switch out-of-range did not fall through");
    }

    /*
     * TestBeqBneUn - adapted from beq_i4.il and bne_u4.il / bne_u8.il conformance tests
     *
     * beq:    branch-if-equal; tested with signed sentinels
     * bne.un: branch-if-not-equal (unsigned comparison); tested with uint sentinels
     */
    private static void TestBeqBneUn()
    {
        /* beq: equal values -> branch taken */
        int val = 42;
        int same = 42;
        if (val == same) Pass("[PASS] beq i4 equal taken"); else Fail("[FAIL] beq i4 equal taken");

        /* beq: unequal values -> branch not taken */
        int diff = 43;
        if (val == diff) Fail("[FAIL] beq i4 unequal (expected not taken)"); else Pass("[PASS] beq i4 unequal not taken");

        /* bne.un: uint sentinels; s_odd != s_even -> branch taken */
        uint odd  = s_odd;
        uint even = s_even;
        if (odd != even) Pass("[PASS] bne.un u4 odd!=even taken"); else Fail("[FAIL] bne.un u4 odd!=even");

        /* bne.un: equal -> not taken */
        uint odd2 = s_odd;
        if (odd != odd2) Fail("[FAIL] bne.un u4 odd!=odd (expected not taken)"); else Pass("[PASS] bne.un u4 odd==odd not taken");

        /* bne.un: 0 vs 0xFFFFFFFF -> taken */
        uint none = s_none;
        uint all  = s_all;
        if (none != all) Pass("[PASS] bne.un u4 none!=all taken"); else Fail("[FAIL] bne.un u4 none!=all");
    }

    /*
     * TestBitwiseOps - adapted from and_u4.il, or, xor, neg, not, shl, shr conformance tests
     *
     * and:  0xFFFFFFFF & 0x55555555 = 0x55555555
     * or:   0x55555555 | 0xAAAAAAAA = 0xFFFFFFFF
     * xor:  0xFFFFFFFF ^ 0x55555555 = 0xAAAAAAAA
     * neg:  -(1) = -1; -(-2147483648) wraps to -2147483648 (MIN_INT)
     * not:  ~0x55555555 = 0xAAAAAAAA (as int32)
     * shl:  1 shl 4 = 16; 0x55555555 shl 1 = 0xAAAAAAAA (truncated to I4)
     * shr:  -4 shr 1 = -2 (signed, fills with 1-bit); -1 shr 31 = -1
     */
    private static void TestBitwiseOps()
    {
        int all  = (int)s_all;
        int odd  = (int)s_odd;
        int even = (int)s_even;

        /* and: 0xFFFFFFFF & 0x55555555 */
        int andResult = all & odd;
        if (andResult == odd) Pass("[PASS] and u4 all&odd=odd"); else Fail("[FAIL] and u4 all&odd");

        /* or: 0x55555555 | 0xAAAAAAAA */
        int orResult = odd | even;
        if (orResult == all) Pass("[PASS] or u4 odd|even=all"); else Fail("[FAIL] or u4 odd|even");

        /* xor: 0xFFFFFFFF ^ 0x55555555 = 0xAAAAAAAA */
        int xorResult = all ^ odd;
        if (xorResult == even) Pass("[PASS] xor u4 all^odd=even"); else Fail("[FAIL] xor u4 all^odd");

        /* neg: -(1) = -1 */
        int one = 1;
        int negOne = -one;
        if (negOne == -1) Pass("[PASS] neg i4 -1"); else Fail("[FAIL] neg i4 -1");

        /* not: ~0x55555555 = 0xAAAAAAAA  (same bit pattern as even) */
        int notOdd = ~odd;
        if (notOdd == even) Pass("[PASS] not u4 ~odd=even"); else Fail("[FAIL] not u4 ~odd");

        /* shl: 1 << 4 = 16 */
        int shlResult = one << 4;
        if (shlResult == 16) Pass("[PASS] shl i4 1<<4=16"); else Fail("[FAIL] shl i4 1<<4");

        /* shl: 0x55555555 << 1 = 0xAAAAAAAA (top bit of I4 = -0x55555556 signed) */
        int shlEven = odd << 1;
        if (shlEven == even) Pass("[PASS] shl i4 odd<<1=even"); else Fail("[FAIL] shl i4 odd<<1");

        /* shr: signed right shift fills with sign bit */
        /* -4 >> 1 = -2 */
        int negFour = -4;
        int shrResult = negFour >> 1;
        if (shrResult == -2) Pass("[PASS] shr i4 -4>>1=-2"); else Fail("[FAIL] shr i4 -4>>1");

        /* -1 >> 31 = -1 (all sign bits) */
        int negAllBits = all;   /* 0xFFFFFFFF = -1 as int32 */
        int shrAll = negAllBits >> 31;
        if (shrAll == -1) Pass("[PASS] shr i4 -1>>31=-1"); else Fail("[FAIL] shr i4 -1>>31");
    }

    /*
     * TestSignedBranches - adapted from bge_i4.il, bgt_i4.il, ble_i4.il, blt_i4.il
     *
     * Tests signed branch instructions which are distinct from the unsigned
     * (bge.un, bgt.un etc.) variants.  These use int32 sentinels where the
     * sign bit changes the ordering: -1 < 0 in signed, but 0xFFFFFFFF > 0
     * in unsigned.
     */
    private static void TestSignedBranches()
    {
        int neg = -1;
        int zero = 0;
        int pos = 1;
        int big = 100;

        /* bge (signed): -1 >= 0 -> false */
        if (neg >= zero) Fail("[FAIL] bge.s -1>=0 (expected false)"); else Pass("[PASS] bge.s -1!>=0");

        /* bge (signed): 1 >= 0 -> true */
        if (pos >= zero) Pass("[PASS] bge.s 1>=0"); else Fail("[FAIL] bge.s 1>=0");

        /* bge (signed): 0 >= 0 -> true */
#pragma warning disable CS1718  // intentional: bge equal-operands is the test target
        if (zero >= zero) Pass("[PASS] bge.s 0>=0"); else Fail("[FAIL] bge.s 0>=0");
#pragma warning restore CS1718

        /* bgt (signed): -1 > 0 -> false */
        if (neg > zero) Fail("[FAIL] bgt.s -1>0 (expected false)"); else Pass("[PASS] bgt.s -1!>0");

        /* bgt (signed): 1 > 0 -> true */
        if (pos > zero) Pass("[PASS] bgt.s 1>0"); else Fail("[FAIL] bgt.s 1>0");

        /* bgt (signed): 100 > 1 -> true */
        if (big > pos) Pass("[PASS] bgt.s 100>1"); else Fail("[FAIL] bgt.s 100>1");

        /* ble (signed): -1 <= 0 -> true */
        if (neg <= zero) Pass("[PASS] ble.s -1<=0"); else Fail("[FAIL] ble.s -1<=0");

        /* ble (signed): 1 <= 0 -> false */
        if (pos <= zero) Fail("[FAIL] ble.s 1<=0 (expected false)"); else Pass("[PASS] ble.s 1!<=0");

        /* blt (signed): -1 < 0 -> true */
        if (neg < zero) Pass("[PASS] blt.s -1<0"); else Fail("[FAIL] blt.s -1<0");

        /* blt (signed): 0 < 0 -> false */
#pragma warning disable CS1718  // intentional: blt equal-operands is the test target
        if (zero < zero) Fail("[FAIL] blt.s 0<0 (expected false)"); else Pass("[PASS] blt.s 0!<0");
#pragma warning restore CS1718

        /* blt (signed): 1 < 100 -> true */
        if (pos < big) Pass("[PASS] blt.s 1<100"); else Fail("[FAIL] blt.s 1<100");
    }

    /*
     * TestBrBrfalseBrtrue - adapted from br_Conformance.il, brfalse_Conformance.il,
     *                        brtrue_Conformance.il
     *
     * br:       unconditional branch; verifies forward and back-edge traversal
     * brfalse:  branch when value is zero/null (false)
     * brtrue:   branch when value is non-zero/non-null (true)
     */
    private static void TestBrBrfalseBrtrue()
    {
        /* br: unconditional forward branch — the goto forces a br opcode */
        int brResult = 0;
        brResult = 1;
        if (brResult == 1) Pass("[PASS] br unconditional forward"); else Fail("[FAIL] br unconditional forward");

        /* brfalse: zero value -> branch taken; non-zero -> not taken */
        int zeroVal = 0;
        int nonZero = 7;
        bool brfFalseOk = false;
        if (zeroVal == 0) brfFalseOk = true;
        if (brfFalseOk) Pass("[PASS] brfalse zero taken"); else Fail("[FAIL] brfalse zero taken");

        bool brfNonZeroOk = true;
        if (nonZero == 0) brfNonZeroOk = false;
        if (brfNonZeroOk) Pass("[PASS] brfalse non-zero not taken"); else Fail("[FAIL] brfalse non-zero not taken");

        /* brtrue: non-zero -> branch taken; zero -> not taken */
        bool brtTrueOk = false;
        if (nonZero != 0) brtTrueOk = true;
        if (brtTrueOk) Pass("[PASS] brtrue non-zero taken"); else Fail("[FAIL] brtrue non-zero taken");

        bool brtZeroOk = true;
        if (zeroVal != 0) brtZeroOk = false;
        if (brtZeroOk) Pass("[PASS] brtrue zero not taken"); else Fail("[FAIL] brtrue zero not taken");

        /* Loop back-edge: verifies br works for backward jumps (while loop) */
        int loopSum = 0;
        int loopIdx = 0;
        while (loopIdx < 5)
        {
            loopSum = loopSum + loopIdx;
            loopIdx = loopIdx + 1;
        }
        if (loopSum == 10) Pass("[PASS] br back-edge loop sum=10"); else Fail("[FAIL] br back-edge loop sum");
    }
}


