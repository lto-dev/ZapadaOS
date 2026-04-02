/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.StaticField.cs
 *
 * Static field 64-bit layout conformance tests.
 *
 * Covers Gap #2 from plans/clr-conformance-gaps.md:
 *   stsfld must store 8 bytes for CLR_KIND_INT64 (I8/U8) values.
 *   ldsfld must read 8 bytes and push CLR_KIND_INT64 for I8/U8 fields.
 *
 * With the bug present:
 *   - stsfld writes only the lower 32 bits ((uint32_t)to_i64(val))
 *   - ldsfld reads only 4 bytes and pushes make_i32() (CLR_KIND_INT32)
 *   - ceq with a 64-bit sentinel whose upper 32 bits are non-zero → FAIL
 *
 * With the fix:
 *   - stsfld detects CLR_KIND_INT64 and writes all 8 bytes
 *   - ldsfld reads the FieldDef signature kind, reads 8 bytes, pushes make_i64()
 *   - all sentinels round-trip correctly → PASS
 *
 * Sentinel values are set in InitStaticFieldSentinels(), called by Initialize()
 * in ConformanceTests.cs, so the C# compiler cannot constant-fold comparisons.
 *
 * Reference: ECMA-335 §III.3.38 (stsfld), §III.3.29 (ldsfld)
 */

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    /*
     * s_sfLong0 — 0x0102030405060708L
     *   Upper half 0x01020304 is non-zero; truncation to 32 bits gives 0x05060708.
     *   ceq with the 64-bit sentinel fails because 0x05060708 != 0x0102030405060708.
     */
    private static long s_sfLong0;

    /*
     * s_sfLong1 — 4294967296L (= 0x0000_0001_0000_0000L, i.e., 1 << 32)
     *   Lower 32 bits are zero; truncation gives 0x00000000.
     *   ceq with 4294967296L fails because 0 != 4294967296.
     */
    private static long s_sfLong1;

    /*
     * s_sfLong2 — long.MinValue (= 0x8000_0000_0000_0000L)
     *   Lower 32 bits are zero; truncation gives 0x00000000.
     *   ceq with long.MinValue fails because 0 != long.MinValue.
     */
    private static long s_sfLong2;

    /*
     * InitStaticFieldSentinels — set static long sentinels for Gap #2 tests.
     * Called by Initialize() in ConformanceTests.cs before Run() dispatches tests.
     */
    private static void InitStaticFieldSentinels()
    {
        s_sfLong0 = 0x0102030405060708L;
        s_sfLong1 = 4294967296L;
        s_sfLong2 = long.MinValue;
    }

    /*
     * TestStaticI8Field
     *
     * Three read-back checks using sentinels set by InitStaticFieldSentinels(),
     * plus one write-from-local round-trip written inline.
     *
     * Each assertion will FAIL with the Gap #2 bug and PASS after the fix.
     */
    private static void TestStaticI8Field()
    {
        /*
         * Check 1: all-different-bytes sentinel.
         * Bug: stsfld writes lower 32 bits (0x05060708); ldsfld reads back
         *      make_i32(0x05060708); to_i64 gives 0x00000000_05060708.
         *      ceq(0x00000000_05060708, 0x0102030405060708) → 0 → FAIL.
         * Fix: full 8 bytes stored and loaded → PASS.
         */
        long v0 = s_sfLong0;
        if (v0 == 0x0102030405060708L)
            Pass("[PASS] static-i8 all-bytes sentinel");
        else
            Fail("[FAIL] static-i8 all-bytes sentinel");

        /*
         * Check 2: upper-half-only sentinel (lower 32 = 0).
         * Bug: stsfld writes 0x00000000; ldsfld reads back 0 → INT32(0).
         *      ceq(0, 4294967296) → 0 → FAIL.
         * Fix: full 8 bytes stored and loaded → PASS.
         */
        long v1 = s_sfLong1;
        if (v1 == 4294967296L)
            Pass("[PASS] static-i8 upper-half sentinel");
        else
            Fail("[FAIL] static-i8 upper-half sentinel");

        /*
         * Check 3: long.MinValue — sign bit only, lower 32 = 0.
         * Bug: stsfld writes 0x00000000 → ldsfld reads 0 → FAIL.
         * Fix: full 8 bytes stored → PASS.
         */
        long v2 = s_sfLong2;
        if (v2 == long.MinValue)
            Pass("[PASS] static-i8 min-value sentinel");
        else
            Fail("[FAIL] static-i8 min-value sentinel");

        /*
         * Check 4: write from local variable, verify round-trip.
         * Store a negative value; if only 32 bits survive, sign-extension
         * would give a different result.
         */
        s_sfLong0 = 0x7E00FF00FF00FF00L;
        long v3 = s_sfLong0;
        if (v3 == 0x7E00FF00FF00FF00L)
            Pass("[PASS] static-i8 store-from-local");
        else
            Fail("[FAIL] static-i8 store-from-local");
    }
}


