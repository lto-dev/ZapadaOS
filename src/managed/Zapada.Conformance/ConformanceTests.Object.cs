/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.Object.cs
 *
 * Object model conformance tests: box/unbox.any, isinst, castclass.
 *
 * Adapted from:
 *   dotnet/runtime/src/tests/JIT/IL_Conformance/Old/objectmodel/
 *     Box_Unbox.il, isinst.il, castclass.il
 */

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    private struct PointProbe
    {
        internal int X;
        internal int Y;
    }

    private sealed class CastProbe
    {
        internal int Value = 7;
    }

    /*
     * TestBoxingAndTypeChecks
     *
     * Runtime-aligned intent:
     * - Box_Unbox.il    — box an int and long, unbox.any them back
     * - isinst.il       — positive and negative isinst (as T?)
     * - castclass.il    — successful castclass (derived-to-base style)
     *
     * This managed version stays small enough for Zapada while keeping the
     * same semantic expectations: successful box/unbox.any, positive/negative
     * `isinst`, and valid `castclass` for a derived-to-base style object flow.
     */
    private static void TestBoxingAndTypeChecks()
    {
        object boxedInt = 123;
        int unboxedInt = (int)boxedInt;
        if (unboxedInt == 123) Pass("[PASS] box/unbox.any int32"); else Fail("[FAIL] box/unbox.any int32");

        object boxedLong = 0x1122334455667788L;
        long unboxedLong = (long)boxedLong;
        if (unboxedLong == 0x1122334455667788L) Pass("[PASS] box/unbox.any int64"); else Fail("[FAIL] box/unbox.any int64");

        object probeObj = new CastProbe();
        CastProbe? probe = probeObj as CastProbe;
        if (probe != null && probe.Value == 7) Pass("[PASS] isinst cast-probe"); else Fail("[FAIL] isinst cast-probe");

        string text = "BOX";
        object textObj = text;
        string? backToString = textObj as string;
        if (backToString != null) Pass("[PASS] isinst string-object positive"); else Fail("[FAIL] isinst string-object positive");

        CastProbe? reject = textObj as CastProbe;
        if (reject == null) Pass("[PASS] isinst negative"); else Fail("[FAIL] isinst negative");

        CastProbe casted = (CastProbe)probeObj;
        if (casted != null && casted.Value == 7) Pass("[PASS] castclass cast-probe"); else Fail("[FAIL] castclass cast-probe");

        PointProbe point = default;
        point.X = 12;
        point.Y = 34;
        PointProbe copy = point;
        if (copy.X == 12 && copy.Y == 34) Pass("[PASS] valuetype point local round-trip"); else Fail("[FAIL] valuetype point local round-trip");
    }
}


