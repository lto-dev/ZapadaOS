/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.ExceptionType.cs
 *
 * Exception type matching and isinst inheritance conformance tests.
 *
 * Covers Gap #4 from plans/clr-conformance-gaps.md:
 *   OP_THROW must check the thrown object's type against each catch clause's
 *   declared catch type (TypeDef/TypeRef token in the EH section) and only
 *   dispatch to a handler when the thrown type IS-A the catch type.
 *
 * Covers Gap #13 from plans/clr-conformance-gaps.md:
 *   isinst / castclass must walk the TypeDef.Extends inheritance chain rather
 *   than performing a flat equality check on the typedef row.
 *
 * Design of test exception hierarchy
 * -----------------------------------
 * We define a minimal exception hierarchy within this assembly:
 *
 *   ExBase (extends object — NOT extending System.Exception because
 *           System.Exception is a cross-assembly type requiring full BCL)
 *   ExTypeA extends ExBase
 *   ExTypeB extends ExBase
 *   ExTypeDerived extends ExTypeA
 *
 * With Gap #4 unfixed (before this session), the first catch clause in the
 * try range always fires regardless of the thrown type, causing wrong output.
 *
 * With Gap #13 unfixed (before this session), isinst on a base type returns
 * null for derived instances because the type check was exact-match only.
 *
 * Both gaps are now fixed via the CLR type registry (clr_type.h):
 *   - newobj registers each type with its parent_type_id at allocation time
 *   - clr_object_matches_type_token() calls clr_type_is_subtype_of() which
 *     walks the parent chain recorded in g_clr_types[]
 *
 * Reference:
 *   Mono CLONES/runtime/src/mono/mono/mini/mini-exceptions.c:2055
 *     mono_object_isinst_checked(ex_obj, catch_class, error)
 *   CoreCLR CLONES/runtime/src/coreclr/vm/typehandle.cpp
 *     TypeHandle::IsAssignableTo()
 */

using System;

namespace Zapada.Conformance;

/*
 * Minimal exception type hierarchy used by the tests below.
 *
 * All types extend Exception (required by the C# compiler for throw/catch).
 * ExBase extends Exception (cross-assembly TypeRef to System.Runtime).
 * ExTypeA, ExTypeB extend ExBase (same-assembly TypeDef).
 * ExTypeDerived extends ExTypeA (two-level inheritance depth).
 *
 * At runtime in the Zapada kernel:
 *   - System.Exception is a cross-assembly TypeRef; our resolver will not
 *     find it in any loaded assembly but returns CLR_TYPE_EXCEPTION.
 *   - ExBase.parent_type_id = CLR_TYPE_EXCEPTION in the type registry.
 *   - ExTypeA.parent_type_id = type_id(ExBase).
 *   - ExTypeDerived.parent_type_id = type_id(ExTypeA).
 *
 * The tests check that clr_type_is_subtype_of() correctly walks this chain.
 */
internal class ExBase        : Exception { public int Code; }
internal class ExTypeA       : ExBase { }
internal class ExTypeB       : ExBase { }
internal class ExTypeDerived : ExTypeA { }

internal static partial class ConformanceTests
{
    /*
     * TestExceptionTypeCatch
     *
     * Verifies that catch clauses correctly filter by thrown type.
     * Without Gap #4 fix: first catch in range fires for any thrown type.
     * With fix: only the matching catch fires.
     */
    internal static void TestExceptionTypeCatch()
    {
        /*
         * Test 1: throw ExTypeA, catch ExTypeA should fire, ExTypeB should not.
         */
        {
            int which = 0;
            ExTypeA a = new ExTypeA();
            a.Code = 1;
            try
            {
                throw a;
            }
            catch (ExTypeB)
            {
                which = -1; /* should NOT fire */
            }
            catch (ExTypeA)
            {
                which = 1;  /* should fire */
            }
            if (which == 1)
                Pass("[PASS] catch ExTypeA fires for ExTypeA");
            else
                Fail("[FAIL] catch ExTypeA fires for ExTypeA");
        }

        /*
         * Test 2: throw ExTypeB, catch ExTypeA should not fire, ExTypeB should.
         */
        {
            int which = 0;
            ExTypeB b = new ExTypeB();
            b.Code = 2;
            try
            {
                throw b;
            }
            catch (ExTypeA)
            {
                which = -1; /* should NOT fire */
            }
            catch (ExTypeB)
            {
                which = 2;  /* should fire */
            }
            if (which == 2)
                Pass("[PASS] catch ExTypeB fires for ExTypeB");
            else
                Fail("[FAIL] catch ExTypeB fires for ExTypeB");
        }

        /*
         * Test 3: throw ExTypeDerived, catch ExTypeA (base) should fire
         * because ExTypeDerived IS-A ExTypeA through inheritance.
         */
        {
            int which = 0;
            ExTypeDerived d = new ExTypeDerived();
            d.Code = 3;
            try
            {
                throw d;
            }
            catch (ExTypeB)
            {
                which = -1; /* should NOT fire */
            }
            catch (ExTypeA)
            {
                which = 3;  /* should fire: ExTypeDerived IS-A ExTypeA */
            }
            if (which == 3)
                Pass("[PASS] catch ExTypeA fires for ExTypeDerived (derived class)");
            else
                Fail("[FAIL] catch ExTypeA fires for ExTypeDerived (derived class)");
        }

        /*
         * Test 4: throw ExTypeDerived, catch ExBase (grandparent) should fire.
         */
        {
            int which = 0;
            ExTypeDerived d = new ExTypeDerived();
            d.Code = 4;
            try
            {
                throw d;
            }
            catch (ExBase)
            {
                which = 4;  /* should fire: ExTypeDerived IS-A ExBase */
            }
            if (which == 4)
                Pass("[PASS] catch ExBase fires for ExTypeDerived (grandparent class)");
            else
                Fail("[FAIL] catch ExBase fires for ExTypeDerived (grandparent class)");
        }

        /*
         * Test 5: no matching catch → exception propagates, caught by outer catch.
         */
        {
            int outer = 0;
            ExTypeA a = new ExTypeA();
            a.Code = 5;
            try
            {
                try
                {
                    throw a;
                }
                catch (ExTypeB)
                {
                    outer = -1; /* should NOT fire */
                }
            }
            catch (ExTypeA)
            {
                outer = 5;  /* should fire in the outer try */
            }
            if (outer == 5)
                Pass("[PASS] unmatched inner catch propagates to outer handler");
            else
                Fail("[FAIL] unmatched inner catch propagates to outer handler");
        }
    }

    /*
     * TestIsinstInheritance
     *
     * Verifies that isinst returns non-null for derived instances checked
     * against a base class type, and null for unrelated types.
     *
     * Without Gap #13 fix: isinst performs exact typedef-row equality,
     * so ExTypeDerived isinst ExTypeA returns null even though it is a subtype.
     *
     * With fix: clr_object_matches_type_token uses clr_type_is_subtype_of()
     * which walks the base_type chain in the type registry.
     */
    internal static void TestIsinstInheritance()
    {
        /*
         * Test 1: exact type match — should return non-null.
         */
        ExTypeA a = new ExTypeA();
        a.Code = 10;
        object aObj = a;
        ExTypeA castA = aObj as ExTypeA;
        if (castA != null)
            Pass("[PASS] isinst exact match ExTypeA is ExTypeA");
        else
            Fail("[FAIL] isinst exact match ExTypeA is ExTypeA");

        /*
         * Test 2: derived instance IS-A base — should return non-null.
         */
        ExTypeDerived d = new ExTypeDerived();
        d.Code = 20;
        object dObj = d;
        ExTypeA castBase = dObj as ExTypeA;
        if (castBase != null)
            Pass("[PASS] isinst derived ExTypeDerived is ExTypeA");
        else
            Fail("[FAIL] isinst derived ExTypeDerived is ExTypeA");

        /*
         * Test 3: derived IS-A grandparent.
         */
        ExBase castGrand = dObj as ExBase;
        if (castGrand != null)
            Pass("[PASS] isinst derived ExTypeDerived is ExBase");
        else
            Fail("[FAIL] isinst derived ExTypeDerived is ExBase");

        /*
         * Test 4: unrelated type check — should return null.
         */
        ExTypeB bCast = dObj as ExTypeB;
        if (bCast == null)
            Pass("[PASS] isinst unrelated ExTypeDerived is not ExTypeB");
        else
            Fail("[FAIL] isinst unrelated ExTypeDerived is not ExTypeB");

        /*
         * Test 5: ExTypeB IS-A ExBase (sibling hierarchy branch).
         */
        ExTypeB b = new ExTypeB();
        b.Code = 50;
        object bObj = b;
        ExBase bAsBase = bObj as ExBase;
        if (bAsBase != null)
            Pass("[PASS] isinst ExTypeB is ExBase");
        else
            Fail("[FAIL] isinst ExTypeB is ExBase");

        /*
         * Test 6: ExTypeA is NOT ExTypeB.
         */
        ExTypeB aCastB = aObj as ExTypeB;
        if (aCastB == null)
            Pass("[PASS] isinst ExTypeA is not ExTypeB");
        else
            Fail("[FAIL] isinst ExTypeA is not ExTypeB");
    }
}


