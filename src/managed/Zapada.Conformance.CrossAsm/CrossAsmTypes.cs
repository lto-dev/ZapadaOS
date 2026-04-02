namespace Zapada.Conformance.CrossAsm;

public abstract class CrossAsmBase
{
    public int Number;
    public int Other;

    protected CrossAsmBase(int number, int other)
    {
        Number = number;
        Other = other;
    }

    public void SetNumber(int value)
    {
        Number = value;
    }

    public void SetOther(int value)
    {
        Other = value;
    }

    public abstract int Compute();
}

public sealed class CrossAsmDerived : CrossAsmBase
{
    public static int GlobalCount;

    public CrossAsmDerived(int number, int other)
        : base(number, other)
    {
    }

    public override int Compute()
    {
        return Number + Other + GlobalCount;
    }
}

public static class CrossAsmDelegateTargets
{
    public static int Multiply(int left, int right)
    {
        return left * right;
    }

    public static string GetLabel()
    {
        return "CrossAsm";
    }
}
