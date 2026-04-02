/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.Generics.cs
 *
 * Generic method (MethodSpec token 0x2B) conformance tests.
 *
 * Covers Gap #1a from plans/clr-conformance-gaps.md:
 *   call/callvirt with a MethodSpec token (generic method instantiation)
 *   must decode the MethodSpec row to find the underlying MethodDef or
 *   MemberRef, then resolve and execute the method body.
 *
 * With the bug present:
 *   resolve_method_token hits the default "unsupported method token table: 0x2B"
 *   path and returns false, aborting the method execution entirely.
 *
 * With the original fix:
 *   resolve_method_token reads the MethodSpec.Method coded index, reconstructs
 *   the parent MethodDef or MemberRef token, and delegates to the existing
 *   resolver.  That is enough for erased generic methods whose runtime behavior
 *   does not depend on the actual instantiation.
 *
 * Important limitation of this test file:
 *   these tests validate only the "generic method token + erased execution"
 *   slice. They do NOT validate the full generic runtime. In particular they do
 *   not require:
 *     - closed generic type identity (List<int> vs List<string>)
 *     - generic field layout inflation for fields of type T
 *     - generic interface ownership/dispatch such as IEnumerable<int>
 *     - generic context lookup from shared code (VAR / MVAR runtime resolution)
 *
 * So this file could pass before the full generic runtime existed, because the
 * covered methods GenericIdentity<T>(), GenericMax<T>(), and
 * GenericAccumulate<T>() do not need specialization-aware runtime semantics.
 *
 * Reference: ECMA-335 §II.22.29 (MethodSpec table);
 *            CoreCLR memberload.cpp GetMethodDescFromMethodSpec().
 *
 * Test strategy: the C# compiler emits MethodSpec tokens whenever a generic
 * method is called with a concrete type argument. All calls below therefore
 * exercise 0x2B-table token resolution, but only in the erased generic-method
 * subset.
 */

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    /*
     * Generic identity method — emits MethodSpec on every call.
     * The compiler cannot inline or fold this because it's a non-trivial
     * method with a type parameter.  The method body simply returns its
     * argument, which in the erased model is the same int32/int64/objref
     * the caller passed in.
     */
    private static T GenericIdentity<T>(T value)
    {
        return value;
    }

    /*
     * Generic max — takes two ints (disguised as generic) and returns
     * the larger.  More interesting than identity: exercises the generic
     * method body with actual control flow.
     */
    private static int GenericMax<T>(int a, int b)
    {
        if (a > b)
            return a;
        return b;
    }

    /*
     * Generic sum — exercises a loop body in a generic method.
     */
    private static int GenericAccumulate<T>(int start, int count)
    {
        int sum = start;
        int i = 0;
        while (i < count)
        {
            sum = sum + i;
            i = i + 1;
        }
        return sum;
    }

    internal static void TestGenericMethods()
    {
        /*
         * Test 1: int identity via MethodSpec.
         * Call: GenericIdentity<int>(42) -> 42
         * Without fix: resolve_method_token returns false, method aborts.
         * With fix: MethodSpec decoded, GenericIdentity body executes, returns 42.
         */
        int v1 = GenericIdentity<int>(42);
        if (v1 == 42)
            Pass("[PASS] MethodSpec int identity");
        else
            Fail("[FAIL] MethodSpec int identity");

        /*
         * Test 2: long identity via MethodSpec.
         * The type argument changes but the body is the same erased code.
         */
        long v2 = GenericIdentity<long>(0x0102030405060708L);
        if (v2 == 0x0102030405060708L)
            Pass("[PASS] MethodSpec long identity");
        else
            Fail("[FAIL] MethodSpec long identity");

        /*
         * Test 3: generic method with control flow.
         * GenericMax<int>(10, 20) -> 20
         */
        int v3 = GenericMax<int>(10, 20);
        if (v3 == 20)
            Pass("[PASS] MethodSpec generic-max a<b");
        else
            Fail("[FAIL] MethodSpec generic-max a<b");

        int v4 = GenericMax<int>(100, 7);
        if (v4 == 100)
            Pass("[PASS] MethodSpec generic-max a>b");
        else
            Fail("[FAIL] MethodSpec generic-max a>b");

        /*
         * Test 4: generic method with loop.
         * GenericAccumulate<int>(0, 5) = 0+0+1+2+3+4 = 10
         */
        int v5 = GenericAccumulate<int>(0, 5);
        if (v5 == 10)
            Pass("[PASS] MethodSpec generic-accumulate loop");
        else
            Fail("[FAIL] MethodSpec generic-accumulate loop");

        /*
         * Test 5: string identity via MethodSpec (OBJREF kind).
         * GenericIdentity<string>("Zapada") -> "Zapada"
         * Exercises that the OBJREF value passes through the erased body
         * correctly.
         */
        string v6 = GenericIdentity<string>("Zapada");
        if (v6 == "Zapada")
            Pass("[PASS] MethodSpec string identity");
        else
            Fail("[FAIL] MethodSpec string identity");
    }
}


