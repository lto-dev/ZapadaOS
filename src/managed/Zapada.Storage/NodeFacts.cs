namespace Zapada.Storage;

public sealed class NodeFacts
{
    public int NodeHandle;
    public int NodeKind;      // 0=unknown, 1=file, 2=directory
    public long Size;
    public long ModifiedTime;
    public int Permissions;
}
