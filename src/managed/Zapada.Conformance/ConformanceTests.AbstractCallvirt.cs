/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.AbstractCallvirt.cs
 *
 * Abstract callvirt conformance tests: callvirt on abstract base class
 * overrides, virtual method dispatch with default implementations, and
 * mixed concrete/abstract dispatch patterns.
 */

namespace Zapada.Conformance;

internal abstract class AbstractAnimal
{
    public abstract int GetLegs();
    public abstract string GetName();
    public virtual int GetEyes() { return 2; }  // virtual with default
}

internal sealed class ConcretesDog : AbstractAnimal
{
    public override int GetLegs() { return 4; }
    public override string GetName() { return "Dog"; }
    // GetEyes inherited (returns 2)
}

internal sealed class ConcretesSpider : AbstractAnimal
{
    public override int GetLegs() { return 8; }
    public override string GetName() { return "Spider"; }
    public override int GetEyes() { return 8; }  // override virtual
}

internal static partial class ConformanceTests
{
    /*
     * TestAbstractCallvirt
     *
     * Verifies callvirt dispatch through abstract base class references:
     *   1. Abstract method dispatch to correct concrete override
     *   2. Virtual method with default (not overridden) via base ref
     *   3. Virtual method overridden in derived via base ref
     *   4. Direct call on concrete type still works
     *   5. Two different objects, same base ref pattern, combined result
     */
    private static void TestAbstractCallvirt()
    {
        // Test 1: abstract callvirt dispatches to Dog
        AbstractAnimal a1 = new ConcretesDog();
        if (a1.GetLegs() == 4) Pass("[PASS] abstract-callvirt dog.GetLegs"); else Fail("[FAIL] abstract-callvirt dog.GetLegs");

        // Test 2: abstract callvirt dispatches to Spider
        AbstractAnimal a2 = new ConcretesSpider();
        if (a2.GetLegs() == 8) Pass("[PASS] abstract-callvirt spider.GetLegs"); else Fail("[FAIL] abstract-callvirt spider.GetLegs");

        // Test 3: abstract callvirt returning string (Dog)
        AbstractAnimal a3 = new ConcretesDog();
        if (a3.GetName() == "Dog") Pass("[PASS] abstract-callvirt dog.GetName"); else Fail("[FAIL] abstract-callvirt dog.GetName");

        // Test 4: abstract callvirt returning string (Spider)
        AbstractAnimal a4 = new ConcretesSpider();
        if (a4.GetName() == "Spider") Pass("[PASS] abstract-callvirt spider.GetName"); else Fail("[FAIL] abstract-callvirt spider.GetName");

        // Test 5: virtual (non-abstract) with default, NOT overridden
        AbstractAnimal a5 = new ConcretesDog();
        if (a5.GetEyes() == 2) Pass("[PASS] virtual-default dog.GetEyes"); else Fail("[FAIL] virtual-default dog.GetEyes");

        // Test 6: virtual overridden in Spider
        AbstractAnimal a6 = new ConcretesSpider();
        if (a6.GetEyes() == 8) Pass("[PASS] virtual-override spider.GetEyes"); else Fail("[FAIL] virtual-override spider.GetEyes");

        // Test 7: direct call (not through base) still works
        ConcretesDog d = new ConcretesDog();
        if (d.GetLegs() == 4) Pass("[PASS] direct-call dog.GetLegs"); else Fail("[FAIL] direct-call dog.GetLegs");

        // Test 8: two different objects, same base ref pattern
        AbstractAnimal a = new ConcretesDog();
        AbstractAnimal b = new ConcretesSpider();
        int sum = a.GetLegs() + b.GetLegs();
        if (sum == 12) Pass("[PASS] abstract-callvirt sum legs"); else Fail("[FAIL] abstract-callvirt sum legs");
    }
}
