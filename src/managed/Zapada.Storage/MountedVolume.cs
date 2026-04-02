namespace Zapada.Storage;

public abstract class MountedVolume
{
    public abstract string GetDriverKey();
    public abstract string GetDisplayName();
    public abstract string GetVolumeLabel();
    public abstract string GetVolumeId();
    public abstract int Resolve(string path);
    public abstract int GetRoot();
    public abstract int ListDirectory(int nodeHandle, DirectoryEntrySink sink);
    public abstract int Stat(int nodeHandle, NodeFacts facts);
    public abstract int Open(int nodeHandle, int accessIntent);
    public abstract int Read(int fileToken, byte[] buffer, int offset, int count);
    public abstract int Seek(int fileToken, long absoluteOffset);
    public abstract int Close(int fileToken);

    public virtual string? GetServiceKey() { return null; }
}
