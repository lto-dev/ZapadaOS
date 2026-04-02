namespace Zapada.Storage;

public abstract class DirectoryEntrySink
{
    public abstract void OnEntry(int nodeHandle, string name, int nodeKind);
}
