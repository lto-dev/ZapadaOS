/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.cs
 *
 * IL conformance test suite — main entry point, shared infrastructure.
 *
 * Tests are split across partial-class files by concern:
 *   ConformanceTests.Branch.cs    - bge.un, bgt.un, ble.un, blt.un, beq, bne.un, bitwise ops
 *   ConformanceTests.Arith.cs     - shr.un, div.un, rem.un, conv.*, ceq, cgt, clt (I4/I8)
 *   ConformanceTests.Stack.cs     - dup, pop, starg, ldarg
 *   ConformanceTests.Array.cs     - array helpers, indexed fill/sum, large local counts
 *   ConformanceTests.String.cs    - string indexing, path parsing, substring, chained equality
 *   ConformanceTests.Object.cs    - box/unbox.any, isinst, castclass
 *   ConformanceTests.StringBcl.cs - StartsWith, EndsWith, Contains, Compare, ToUpper, ToLower, Trim, Replace
 *   ConformanceTests.AbstractCallvirt.cs - callvirt on abstract base overrides, virtual default dispatch
 *   ConformanceTests.I8.cs        - I8 arithmetic, bitwise, shifts, branch comparisons
 *   ConformanceTests.Cctor.cs     - .cctor runs-once, static field init
 *
 * Sentinel values are stored in static fields so the C# compiler cannot
 * constant-fold the comparisons; they are set by Initialize() as a
 * belt-and-suspenders guard (the CLR .cctor infrastructure also runs them at
 * assembly load time via clr_run_all_cctors).
 *
 * uint field comparisons (>=, >, <=, <) generate bge.un / bgt.un / ble.un /
 * blt.un in the emitted IL.  uint >> n generates shr.un.  uint / uint
 * generates div.un.  uint % uint generates rem.un.
 */

using Zapada.Conformance.Runtime;

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    /*
     * Sentinel values (I4 unsigned patterns).
     *   s_all  = 0xFFFFFFFF  — all bits set
     *   s_none = 0x00000000  — no bits set
     *   s_odd  = 0x55555555  — odd bits
     *   s_even = 0xAAAAAAAA  — even bits (larger than s_odd as uint)
     */
    private static uint s_all;
    private static uint s_none;
    private static uint s_odd;
    private static uint s_even;

    private static int s_pass;
    private static int s_fail;

    /*
     * Initialize — must be called before Run().
     * Sets sentinel values and resets pass/fail counters.
     */
    private static void Initialize()
    {
        s_all  = 0xFFFFFFFFu;
        s_none = 0x00000000u;
        s_odd  = 0x55555555u;
        s_even = 0xAAAAAAAAu;
        s_pass = 0;
        s_fail = 0;

        /* Gap #2: static field 64-bit sentinels */
        InitStaticFieldSentinels();
    }

    private static void Pass(string name)
    {
        InternalCalls.Write("[Conf] PASS ");
        InternalCalls.Write(name);
        InternalCalls.Write("\n");
        s_pass = s_pass + 1;
    }

    private static void Fail(string name)
    {
        InternalCalls.Write("[Conf] FAIL ");
        InternalCalls.Write(name);
        InternalCalls.Write("\n");
        s_fail = s_fail + 1;
    }

    /*
     * Run - entry point called from DllMain.Initialize().
     *
     * Calls Initialize() to set sentinel values and reset counters, then runs
     * all test groups.  Prints per-group PASS/FAIL lines and a summary
     * "[Conf] pass=N fail=M".
     *
     * Returns 0 if all assertions passed, -1 if any failed.
     */
    internal static int Run()
    {
        Initialize();

        /* GC first so runtime verification reaches GC before broader CoreLib string coverage. */
        TestGcBasic();
        TestGcHandleBasics();
        TestWeakHandleBasic();
        TestFinalizationBasics();
        TestSuppressFinalize();
        TestReRegisterForFinalize();

        /* Branch / bitwise */
        TestBgeUn();
        TestBgtUn();
        TestBleBlUn();
        TestSwitch();
        TestBeqBneUn();
        TestBitwiseOps();
        TestSignedBranches();
        TestBrBrfalseBrtrue();

        /* Arithmetic / conversion */
        TestShrUn();
        TestDivRemUn();
        TestConvOps();
        TestCmpOps();
        TestSignedDivRem();
        TestCheckedArithmetic();
        TestFloatArithmetic();
        TestNop();

        /* Stack / argument round-trips */
        TestDupPop();
        TestArgRoundTrips();

        /* Array / local */
        TestArrayHelperLoops();
        TestIndexedArrayRoundTrips();

        /* String / path */
        TestStringIndexingAndBranches();
        TestPathParsingLoops();
        TestStringSubstringPaths();
        TestChainedEqualityChecks();
        TestUtf16LiteralAndIndexing();
        TestCoreLibStringContractBasics();
        /* Object model */
        TestBoxingAndTypeChecks();

        /* BCL string methods */
        TestStringStartsWith();
        TestStringEndsWith();
        TestStringContains();
        TestStringCompare();
        TestStringToUpper();
        TestStringToLower();
        TestStringTrim();
        TestStringReplace();
        TestUtf16BclBehaviors();
        TestCoreLibStringBehaviorShapes();

        /* I8 */
        TestI8Arithmetic();
        TestI8Bitwise();
        TestI8Shifts();
        TestI8Branches();

        /* Static constructor */
        TestStaticConstructor();

        /* Gap #2: static field 64-bit layout (ldsfld/stsfld I8 round-trip) */
        TestStaticI8Field();

        /* Gap #1: MethodSpec — generic method calls */
        TestGenericMethods();

        /* Gap #4 + Gap #13: exception type matching + isinst inheritance */
        TestExceptionTypeCatch();
        TestIsinstInheritance();

        /* Gap #3: callvirt overload dispatch by param count */
        TestCallvirtOverloadDispatch();

        /* sizeof primitive constants + ldtoken type identity */
        TestSizeofOps();
        TestLdtokenTypeIdentity();

        /* Abstract callvirt dispatch (callvirt on abstract base override) */
        TestAbstractCallvirt();

        /* Phase A/B: cross-assembly fields, static fields, and virtual dispatch */
        TestCrossAssemblyFieldAndDispatch();

        /* Phase H: delegate and lambda support */
        TestDelegates();

        /* Phase I: interface dispatch */
        TestInterfaceDispatch();

        /* Phase J: exception handling completeness */
        TestEhNestedFinallyLeave();
        TestEhNestedFinallyThrowCatch();
        TestEhFilterClause();
        TestEhFilterRejectFallsThrough();
        TestEhCatchInsideFinallySequence();
        TestEhNestedCatchSelection();

        /* Generic owner / runtime identity tests moved late to isolate whether
         * they trigger corruption in earlier conformance groups. */
        TestCoreLibGenericEmptyArrayShapes();
        TestGenericStaticFieldIsolation();
        TestGenericStaticMethodOwnerResolution();
        TestGenericInstanceMethodOwnerResolution();
        TestNestedGenericOwnerIdentity();

        InternalCalls.Write("[Conf] pass=");
        InternalCalls.WriteInt(s_pass);
        InternalCalls.Write(" fail=");
        InternalCalls.WriteInt(s_fail);
        InternalCalls.Write("\n");

        if (s_fail == 0)
            return 0;
        return -1;
    }
}


