namespace Zapada.Fs.Ext;

public static class ExtFeatureFlags
{
    public const int IncompatCompression = 0x0001;
    public const int IncompatFileType = 0x0002;
    public const int IncompatNeedsRecovery = 0x0004;
    public const int IncompatJournalDevice = 0x0008;
    public const int IncompatMetaBlockGroups = 0x0010;
    public const int IncompatExtents = 0x0040;
    public const int IncompatSixtyFourBit = 0x0080;
    public const int IncompatFlexBlockGroups = 0x0200;

    public const int IncompatSupportedReadOnly = IncompatFileType
        | IncompatExtents
        | IncompatSixtyFourBit
        | IncompatFlexBlockGroups;

    public const int ReadOnlyCompatBigAlloc = 0x0200;

    public const int InodeFlagExtents = 0x00080000;

    public const int ModeFileTypeMask = 0xF000;
    public const int ModeDirectory = 0x4000;
    public const int ModeRegularFile = 0x8000;
    public const int ModeSymlink = 0xA000;

    public static bool HasUnsupportedIncompat(int features)
    {
        return (features & ~IncompatSupportedReadOnly) != 0;
    }

    public static bool HasUnsupportedReadOnlyCompat(int features)
    {
        return (features & ReadOnlyCompatBigAlloc) != 0;
    }
}
