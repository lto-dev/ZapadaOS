/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.Stack.cs
 *
 * Stack, dup, pop, and argument round-trip conformance tests.
 *
 * Adapted from:
 *   dotnet/runtime/src/tests/JIT/IL_Conformance/Old/Conformance_Base/
 *     dup4.il, dup8.il, pop4.il, pop8.il, starg_i4.il, ldarg_i4.il
 */

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    /*
     * TestDupPop - tests dup (duplicate top of stack) and pop (discard top of stack)
     *
     * In C#, dup appears in patterns like:
     *   x = expr;    when the value of expr is also used immediately
     *   Nested writes: arr[i] = (arr[i] = value) patterns
     *
     * pop appears when a method return value is discarded.
     */
    private static int ReturnValue(int x) { return x + 1; }

    private static void TestDupPop()
    {
        /* dup: assign and immediately compare in combined expression
         * The C# compiler emits dup when the result of an assignment is used.
         * Pattern: int y = (x = expr); uses dup on expr result. */
        int x = 0;
        int y = x = 7;    /* Assignment expression: rhs stored to x, dup, stored to y */
        if (x == 7 && y == 7) Pass("[PASS] dup assignment chain"); else Fail("[FAIL] dup assignment chain");

        /* pop: discard a call return value (stores return in temp, pops it) */
        ReturnValue(100);   /* return value discarded -> pop in IL */
        Pass("[PASS] pop discard return value");

        /* dup pattern via conditional: the compiler may dup the test value */
        int check = 5;
        int result = (check > 0) ? check : -check;  /* ternary -> dup in cond */
        if (result == 5) Pass("[PASS] dup ternary positive"); else Fail("[FAIL] dup ternary positive");

        check = -3;
        result = (check > 0) ? check : -check;
        if (result == 3) Pass("[PASS] dup ternary negative abs"); else Fail("[FAIL] dup ternary negative abs");
    }

    /*
     * TestArgRoundTrips - explicit ldarg / starg round-trips
     *
     * In C#, starg appears when a method parameter is directly assigned to:
     *   void M(int x) { x = x + 1; }  ->  ldarg.0; ldc.i4.1; add; starg.s 0; ...
     */
    private static int AddToArg(int x, int delta)
    {
        x = x + delta;   /* starg: stores (x + delta) back into arg slot 0 */
        return x;        /* ldarg: reads modified arg slot */
    }

    private static int ClampArg(int value, int lo, int hi)
    {
        if (value < lo) value = lo;   /* starg on underflow */
        if (value > hi) value = hi;   /* starg on overflow */
        return value;
    }

    private static void TestArgRoundTrips()
    {
        /* starg basic: add 10 to arg, read it back */
        int r1 = AddToArg(5, 10);
        if (r1 == 15) Pass("[PASS] starg add-to-arg 5+10=15"); else Fail("[FAIL] starg add-to-arg");

        /* starg with negative delta */
        int r2 = AddToArg(100, -42);
        if (r2 == 58) Pass("[PASS] starg add-to-arg 100-42=58"); else Fail("[FAIL] starg add-to-arg neg");

        /* starg clamp: within range, no change */
        int r3 = ClampArg(50, 0, 100);
        if (r3 == 50) Pass("[PASS] starg clamp in-range"); else Fail("[FAIL] starg clamp in-range");

        /* starg clamp: below lo -> clamped to lo */
        int r4 = ClampArg(-5, 0, 100);
        if (r4 == 0) Pass("[PASS] starg clamp underflow"); else Fail("[FAIL] starg clamp underflow");

        /* starg clamp: above hi -> clamped to hi */
        int r5 = ClampArg(200, 0, 100);
        if (r5 == 100) Pass("[PASS] starg clamp overflow"); else Fail("[FAIL] starg clamp overflow");
    }
}


