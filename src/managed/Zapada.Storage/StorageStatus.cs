namespace Zapada.Storage;

public static class StorageStatus
{
    public const int Ok              =  0;
    public const int NotFound        = -1;
    public const int NoMemory        = -2;
    public const int PermissionDenied = -3;
    public const int IoError         = -4;
    public const int InvalidArgument = -5;
    public const int NotSupported    = -6;
    public const int AlreadyExists   = -7;
    public const int NotDirectory    = -8;
    public const int NotFile         = -9;
    public const int TableFull       = -10;
    public const int CorruptedData   = -11;
    public const int EndOfFile       = -12;
    public const int NotMounted      = -13;
    public const int BusyResource    = -14;
}
