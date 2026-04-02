namespace Zapada.Conformance;

using Zapada.Conformance.CrossAsm;
using Zapada.Conformance.Runtime;

internal static partial class ConformanceTests
{
    private delegate int IntBinary(int left, int right);
    private delegate void VoidDelegate();
    private delegate int ZeroArgDelegate();
    private delegate string StringDelegate();
    private delegate int StructDelegate();

    private struct StructCounter
    {
        internal int Value;

        internal int GetValue()
        {
            return Value;
        }
    }

    private class DelegateTarget
    {
        private readonly int _bias;

        internal DelegateTarget(int bias)
        {
            _bias = bias;
        }

        internal int AddWithBias(int left, int right)
        {
            return left + right + _bias;
        }

        internal virtual int VirtualMix(int left, int right)
        {
            return left + right;
        }

        internal static int StaticSub(int left, int right)
        {
            return left - right;
        }
    }

    private sealed class DelegateTargetDerived : DelegateTarget
    {
        internal DelegateTargetDerived(int bias)
            : base(bias)
        {
        }

        internal override int VirtualMix(int left, int right)
        {
            return left * right;
        }
    }

    private static int s_voidCount;

    private static void CountOne()
    {
        s_voidCount = s_voidCount + 1;
    }

    private static void CountTwo()
    {
        s_voidCount = s_voidCount + 2;
    }

    private static int ZeroArgValue()
    {
        return 42;
    }

    private static string RefReturnValue()
    {
        return "DelegateRef";
    }

    private static void TestDelegates()
    {
        IntBinary staticDelegate = DelegateTarget.StaticSub;
        if (staticDelegate(9, 4) == 5)
            Pass("[PASS] delegate ldftn static invoke");
        else
            Fail("[FAIL] delegate ldftn static invoke");

        DelegateTarget target = new DelegateTarget(3);
        IntBinary instanceDelegate = target.AddWithBias;
        if (instanceDelegate(2, 4) == 9)
            Pass("[PASS] delegate ldftn instance invoke");
        else
            Fail("[FAIL] delegate ldftn instance invoke");

        DelegateTarget virtualTarget = new DelegateTargetDerived(1);
        IntBinary virtualDelegate = virtualTarget.VirtualMix;
        if (virtualDelegate(3, 5) == 15)
            Pass("[PASS] delegate ldvirtftn virtual invoke");
        else
            Fail("[FAIL] delegate ldvirtftn virtual invoke");

        int captured = 4;
        IntBinary lambda = (left, right) => left + right + captured;
        if (lambda(1, 2) == 7)
            Pass("[PASS] delegate lambda invoke");
        else
            Fail("[FAIL] delegate lambda invoke");

        ZeroArgDelegate zero = ZeroArgValue;
        if (zero() == 42)
            Pass("[PASS] delegate zero-arg invoke");
        else
            Fail("[FAIL] delegate zero-arg invoke");

        StringDelegate refDelegate = RefReturnValue;
        if (refDelegate() == "DelegateRef")
            Pass("[PASS] delegate reference return");
        else
            Fail("[FAIL] delegate reference return");

        IntBinary crossAsm = CrossAsmDelegateTargets.Multiply;
        if (crossAsm(6, 7) == 42)
            Pass("[PASS] delegate cross-assembly static invoke");
        else
            Fail("[FAIL] delegate cross-assembly static invoke");

        StringDelegate crossAsmRef = CrossAsmDelegateTargets.GetLabel;
        if (crossAsmRef() == "CrossAsm")
            Pass("[PASS] delegate cross-assembly reference return");
        else
            Fail("[FAIL] delegate cross-assembly reference return");

        VoidDelegate count = CountOne;
        count = count + CountTwo;
        s_voidCount = 0;
        count();
        if (s_voidCount == 3)
            Pass("[PASS] delegate multicast combine invoke order");
        else
            Fail("[FAIL] delegate multicast combine invoke order");

        count = count - CountOne;
        s_voidCount = 0;
        count();
        if (s_voidCount == 2)
            Pass("[PASS] delegate multicast remove invoke");
        else
            Fail("[FAIL] delegate multicast remove invoke");

        VoidDelegate sameA = CountOne;
        VoidDelegate sameB = CountOne;
        VoidDelegate diff = CountTwo;
        if (sameA == sameB)
            Pass("[PASS] delegate equality same target");
        else
            Fail("[FAIL] delegate equality same target");

        if (sameA != diff)
            Pass("[PASS] delegate inequality different target");
        else
            Fail("[FAIL] delegate inequality different target");

        StructCounter counter = default;
        counter.Value = 9;
        int directStructValue = counter.GetValue();
        if (directStructValue != 9)
        {
            InternalCalls.Write("[Conf] direct struct actual=");
            InternalCalls.WriteInt(directStructValue);
            InternalCalls.Write("\n");
        }
        StructDelegate structDelegate = counter.GetValue;
        counter.Value = 13;
        int structValue = structDelegate();
        if (structValue == 9)
            Pass("[PASS] delegate struct target boxed copy");
        else
        {
            InternalCalls.Write("[Conf] delegate struct actual=");
            InternalCalls.WriteInt(structValue);
            InternalCalls.Write("\n");
            Fail("[FAIL] delegate struct target boxed copy");
        }

        try
        {
            IntBinary nullDelegate = null!;
            int value = nullDelegate(1, 2);
            if (value == 0)
                Fail("[FAIL] delegate null invoke throws");
            else
                Fail("[FAIL] delegate null invoke throws");
        }
        catch (global::System.Exception)
        {
            Pass("[PASS] delegate null invoke throws");
        }
    }
}
