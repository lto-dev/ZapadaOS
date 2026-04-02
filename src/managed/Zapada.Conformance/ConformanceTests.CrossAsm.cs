using Zapada.Conformance.CrossAsm;

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    private static void TestCrossAssemblyFieldAndDispatch()
    {
        CrossAsmDerived.GlobalCount = 5;

        CrossAsmDerived derived = new CrossAsmDerived(10, 20);
        if (derived.Number == 10) Pass("[PASS] crossasm ldfld direct read"); else Fail("[FAIL] crossasm ldfld direct read");

        derived.Number = 33;
        if (derived.Number == 33) Pass("[PASS] crossasm stfld direct write"); else Fail("[FAIL] crossasm stfld direct write");

        derived.SetOther(7);
        if (derived.Other == 7) Pass("[PASS] crossasm instance method field write"); else Fail("[FAIL] crossasm instance method field write");

        CrossAsmBase asBase = derived;
        if (asBase.Number == 33) Pass("[PASS] crossasm ldfld through base type"); else Fail("[FAIL] crossasm ldfld through base type");

        asBase.SetNumber(40);
        if (derived.Number == 40) Pass("[PASS] crossasm base method stfld"); else Fail("[FAIL] crossasm base method stfld");

        CrossAsmDerived.GlobalCount = 9;
        if (CrossAsmDerived.GlobalCount == 9) Pass("[PASS] crossasm ldsfld/stsfld direct"); else Fail("[FAIL] crossasm ldsfld/stsfld direct");

        if (asBase.Compute() == 56) Pass("[PASS] crossasm callvirt override"); else Fail("[FAIL] crossasm callvirt override");
    }
}
