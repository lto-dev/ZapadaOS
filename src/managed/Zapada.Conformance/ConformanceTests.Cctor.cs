/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.Cctor.cs
 *
 * Static constructor (.cctor) conformance tests.
 *
 * Verifies that the CLR interpreter:
 *   1. Runs the type-initializer exactly once before the first access to any
 *      member of the type (ECMA-335 S-I.9.5.3).
 *   2. Does not re-enter the .cctor if it is accessed again after initialization
 *      (idempotency via the g_cctor_done bitset in interpreter_cctor.inc).
 *   3. Correctly stores I4 and string reference values set in the .cctor body
 *      into static fields, which are then accessible via ldsfld.
 *
 * Implementation notes:
 *   The .cctor for each probe class is triggered by clr_run_cctor_if_needed()
 *   when any method on that type is first dispatched, or at assembly load time
 *   by clr_run_all_cctors() called from managed_runtime.c.  The bitset guard
 *   at interpreter_cctor.inc:80 prevents re-entrant or repeated execution.
 */

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    /*
     * CctorIntProbe — type whose static constructor sets an integer field.
     * Used to verify .cctor runs and stores I4 correctly.
     */
    private static class CctorIntProbe
    {
        internal static int s_value;
        internal static int s_second;

        static CctorIntProbe()
        {
            s_value  = 42;
            s_second = s_value + 1;   /* 43 — depends on s_value being set first */
        }
    }

    /*
     * CctorStringProbe — type whose static constructor initializes a string field.
     * Used to verify .cctor works for reference-type statics (CLR_TYPE_STRING).
     */
    private static class CctorStringProbe
    {
        internal static string s_name;

        static CctorStringProbe()
        {
            s_name = "CCTOR";
        }
    }

    /*
     * CctorCountProbe — type whose static constructor increments a counter.
     * We reset the counter from outside ONLY before the first access; subsequent
     * accesses must NOT re-trigger the .cctor, so s_initCount stays at 1.
     *
     * Note: we cannot truly reset the bitset from managed code; instead we
     * verify the expected post-init value (1) after multiple accesses.
     */
    private static class CctorCountProbe
    {
        internal static int s_initCount;

        static CctorCountProbe()
        {
            s_initCount = s_initCount + 1;   /* First (and only) run: 0+1 = 1 */
        }
    }

    private sealed class CctorFieldAccessProbe
    {
        internal static int s_initCount;
        internal int Value;

        static CctorFieldAccessProbe()
        {
            s_initCount = s_initCount + 1;
        }

        internal CctorFieldAccessProbe(int value)
        {
            Value = value;
        }
    }

    /*
     * TestStaticConstructor
     *
     * Exercises the three probe classes above:
     *   1. I4 static field init (CctorIntProbe)
     *   2. Dependent ordering within a .cctor (s_second = s_value + 1)
     *   3. String static field init (CctorStringProbe)
     *   4. .cctor runs exactly once (CctorCountProbe)
     */
    private static void TestStaticConstructor()
    {
        /* 1. I4 static field set by .cctor */
        int iv = CctorIntProbe.s_value;
        if (iv == 42) Pass("[PASS] cctor i4 field=42"); else Fail("[FAIL] cctor i4 field=42");

        /* 2. Dependent ordering: s_second = s_value + 1 = 43 */
        int sv = CctorIntProbe.s_second;
        if (sv == 43) Pass("[PASS] cctor dependent i4 field=43"); else Fail("[FAIL] cctor dependent i4 field=43");

        /* 3. Repeated read must return same value (bitset guard) */
        int iv2 = CctorIntProbe.s_value;
        if (iv2 == 42) Pass("[PASS] cctor idempotent i4 re-read"); else Fail("[FAIL] cctor idempotent i4 re-read");

        /* 4. String static field set by .cctor */
        string sn = CctorStringProbe.s_name;
        if (sn != null && sn.Length == 5
            && sn[0] == 'C' && sn[1] == 'C' && sn[2] == 'T'
            && sn[3] == 'O' && sn[4] == 'R')
            Pass("[PASS] cctor string field=CCTOR");
        else
            Fail("[FAIL] cctor string field=CCTOR");

        /* 5. .cctor runs exactly once: s_initCount must be 1 after multiple reads */
        int c1 = CctorCountProbe.s_initCount;
        int c2 = CctorCountProbe.s_initCount;
        int c3 = CctorCountProbe.s_initCount;
        if (c1 == 1 && c2 == 1 && c3 == 1) Pass("[PASS] cctor runs-once guard"); else Fail("[FAIL] cctor runs-once guard");

        CctorFieldAccessProbe probe = new CctorFieldAccessProbe(7);
        if (CctorFieldAccessProbe.s_initCount == 1) Pass("[PASS] cctor newobj trigger"); else Fail("[FAIL] cctor newobj trigger");

        int field = probe.Value;
        if (field == 7 && CctorFieldAccessProbe.s_initCount == 1) Pass("[PASS] cctor ldfld trigger"); else Fail("[FAIL] cctor ldfld trigger");

        probe.Value = 11;
        if (probe.Value == 11 && CctorFieldAccessProbe.s_initCount == 1) Pass("[PASS] cctor stfld trigger"); else Fail("[FAIL] cctor stfld trigger");
    }
}


