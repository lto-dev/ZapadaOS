namespace Zapada.Fs.Vfs;

public abstract class ProcFsProvider
{
    public abstract string BuildMounts();
    public abstract string BuildDrivers();
    public abstract string BuildDevices();
    public abstract string BuildPartitions();
    public abstract string BuildInterrupts();
    public abstract string BuildMemInfo();
    public abstract string BuildUptime();
}
