namespace Zapada.Storage;

public sealed class PartitionInfo
{
    public string Name = "";
    public string Label = "";
    public string DeviceName = "";
    public int Index;
    public long StartLba;
    public long EndLba;
    public long SectorCount;
    public int SchemeKind;

    public void Initialize(string name, string label, string deviceName, int index, long startLba, long endLba, int schemeKind)
    {
        Name = name;
        Label = label;
        DeviceName = deviceName;
        Index = index;
        StartLba = startLba;
        EndLba = endLba;
        SectorCount = endLba - startLba + 1;
        SchemeKind = schemeKind;
    }
}
