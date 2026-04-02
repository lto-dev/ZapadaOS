namespace Zapada.Conformance;

internal interface ICalc
{
    int Add(int left, int right);
    string Name();
}

internal interface IAliasLabel
{
    string GetLabel();
}

internal interface IGenericBox<T>
{
    T Get();
}

internal sealed class InterfaceCalc : ICalc, IAliasLabel
{
    public int Add(int left, int right)
    {
        return left + right + 1;
    }

    string IAliasLabel.GetLabel()
    {
        return "explicit-label";
    }

    public string Name()
    {
        return "calc";
    }
}

internal sealed class GenericIntBox : IGenericBox<int>
{
    private int _value;

    internal GenericIntBox(int value)
    {
        _value = value;
    }

    public int Get()
    {
        return _value;
    }
}

internal static partial class ConformanceTests
{
    private static void TestInterfaceDispatch()
    {
        ICalc calc = new InterfaceCalc();
        if (calc.Add(20, 21) == 42)
            Pass("[PASS] interface dispatch add");
        else
            Fail("[FAIL] interface dispatch add");

        if (calc.Name() == "calc")
            Pass("[PASS] interface dispatch name");
        else
            Fail("[FAIL] interface dispatch name");

        IAliasLabel alias = new InterfaceCalc();
        if (alias.GetLabel() == "explicit-label")
            Pass("[PASS] interface explicit implementation");
        else
            Fail("[FAIL] interface explicit implementation");

        IGenericBox<int> genericBox = new GenericIntBox(42);
        if (genericBox.Get() == 42)
            Pass("[PASS] generic interface dispatch int");
        else
            Fail("[FAIL] generic interface dispatch int");
    }
}
