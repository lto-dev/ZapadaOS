/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.Overloads.cs
 *
 * callvirt overload dispatch (method name + parameter count) conformance tests.
 *
 * Covers Gap #3 from plans/clr-conformance-gaps.md:
 *   pe_find_method_in_typedef() matches by method name only.  Two overloads
 *   with the same name but different parameter counts are indistinguishable.
 *   The callvirt virtual override search must also check the parameter count
 *   from the method's signature blob to disambiguate overloads.
 *
 * With Gap #3 unfixed:
 *   Calling the 2-argument overload dispatches to the 1-argument overload
 *   (whichever is found first in the TypeDef's method list), producing the
 *   wrong result.
 *
 * With Gap #3 fixed:
 *   pe_find_method_in_typedef_with_paramcount() matches name AND parameter
 *   count, dispatching to the correct overload.
 *
 * Reference: Mono CLONES/runtime/src/mono/mono/mini/interp/transform.c
 *   mono_method_signature_internal() + parameter-count comparison for callvirt.
 *   CoreCLR CLONES/runtime/src/coreclr/vm/memberload.cpp FindMethod() which
 *   disambiguates overloads by signature after name match.
 *
 * Test structure: OverloadBase defines three overloads of Add():
 *   Add(int a) -> a + 100
 *   Add(int a, int b) -> a + b
 *   Add(int a, int b, int c) -> a + b + c
 *
 * All three are called through a base-class reference to exercise the
 * callvirt override dispatch path (as opposed to direct call dispatch).
 */

namespace Zapada.Conformance;

/*
 * OverloadBase — base class with multiple overloads of the same method name.
 * Using a class with virtual methods ensures the C# compiler emits callvirt
 * for the calls, which goes through the virtual dispatch path in the interpreter.
 */
internal class OverloadBase
{
    internal virtual int Compute(int a)
    {
        return a + 100;
    }

    internal virtual int Compute(int a, int b)
    {
        return a + b;
    }

    internal virtual int Compute(int a, int b, int c)
    {
        return a + b + c;
    }
}

/*
 * OverloadDerived — subclass that overrides all three overloads.
 * The overrides double the inputs so we can verify correct dispatch:
 *   Compute(x)       = 2*x + 200
 *   Compute(x, y)    = 2*x + 2*y
 *   Compute(x, y, z) = 2*x + 2*y + 2*z
 */
internal class OverloadDerived : OverloadBase
{
    internal override int Compute(int a)
    {
        return (a + a) + 200;
    }

    internal override int Compute(int a, int b)
    {
        return (a + a) + (b + b);
    }

    internal override int Compute(int a, int b, int c)
    {
        return (a + a) + (b + b) + (c + c);
    }
}

internal static partial class ConformanceTests
{
    /*
     * TestCallvirtOverloadDispatch
     *
     * All calls go through an OverloadBase reference (callvirt), exercising
     * the virtual override search path in interpreter_call.inc.
     *
     * Without Gap #3 fix: all three calls dispatch to the 1-argument override
     * (the first match in the TypeDef's method list), producing wrong values.
     * With fix: each call dispatches to the correct overload by arity.
     */
    internal static void TestCallvirtOverloadDispatch()
    {
        OverloadBase obj = new OverloadDerived();

        /*
         * Test 1: 1-argument overload — Compute(5) should call Derived.Compute(int)
         * Expected: (5+5) + 200 = 210
         * Without fix: might dispatch to wrong overload or the base implementation.
         */
        int r1 = obj.Compute(5);
        if (r1 == 210)
            Pass("[PASS] callvirt overload Compute(int) -> 210");
        else
            Fail("[FAIL] callvirt overload Compute(int) -> 210");

        /*
         * Test 2: 2-argument overload — Compute(3, 7) should call Derived.Compute(int,int)
         * Expected: (3+3) + (7+7) = 20
         */
        int r2 = obj.Compute(3, 7);
        if (r2 == 20)
            Pass("[PASS] callvirt overload Compute(int,int) -> 20");
        else
            Fail("[FAIL] callvirt overload Compute(int,int) -> 20");

        /*
         * Test 3: 3-argument overload — Compute(1, 2, 4) should call Derived.Compute(int,int,int)
         * Expected: (1+1) + (2+2) + (4+4) = 14
         */
        int r3 = obj.Compute(1, 2, 4);
        if (r3 == 14)
            Pass("[PASS] callvirt overload Compute(int,int,int) -> 14");
        else
            Fail("[FAIL] callvirt overload Compute(int,int,int) -> 14");

        /*
         * Test 4: base class without override — verify base behaviour through
         * a direct OverloadBase instance.
         */
        OverloadBase base_obj = new OverloadBase();
        int rb1 = base_obj.Compute(42);
        if (rb1 == 142)
            Pass("[PASS] callvirt base Compute(int) -> 142");
        else
            Fail("[FAIL] callvirt base Compute(int) -> 142");

        int rb2 = base_obj.Compute(10, 20);
        if (rb2 == 30)
            Pass("[PASS] callvirt base Compute(int,int) -> 30");
        else
            Fail("[FAIL] callvirt base Compute(int,int) -> 30");

        int rb3 = base_obj.Compute(1, 2, 3);
        if (rb3 == 6)
            Pass("[PASS] callvirt base Compute(int,int,int) -> 6");
        else
            Fail("[FAIL] callvirt base Compute(int,int,int) -> 6");
    }
}


