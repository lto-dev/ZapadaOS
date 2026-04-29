namespace Zapada.Fs.Ext;

public sealed class ExtDirectoryRecord
{
    public const int FileTypeRegularFile = 1;
    public const int FileTypeDirectory = 2;
    public const int FileTypeSymlink = 7;

    public int InodeNumber;
    public int RecordLength;
    public int NameLength;
    public int FileType;

    public bool ReadFrom(byte[] buffer, int offset, int limit)
    {
        if (offset < 0 || offset + 8 > limit)
            return false;

        InodeNumber = (int)ExtLittleEndian.ReadUInt32(buffer, offset + 0);
        RecordLength = ExtLittleEndian.ReadUInt16(buffer, offset + 4);
        NameLength = ExtLittleEndian.ReadByte(buffer, offset + 6);
        FileType = ExtLittleEndian.ReadByte(buffer, offset + 7);

        if (RecordLength < 8 || offset + RecordLength > limit)
            return false;

        return NameLength <= RecordLength - 8;
    }

    public bool IsDotName(byte[] buffer, int offset)
    {
        if (NameLength == 1)
            return ExtLittleEndian.ReadByte(buffer, offset + 8) == '.';

        if (NameLength == 2)
            return ExtLittleEndian.ReadByte(buffer, offset + 8) == '.'
                && ExtLittleEndian.ReadByte(buffer, offset + 9) == '.';

        return false;
    }

    public bool NameMatches(string path, int componentStart, int componentLength, byte[] buffer, int offset)
    {
        if (componentLength != NameLength)
            return false;

        for (int i = 0; i < componentLength; i++)
        {
            int diskByte = ExtLittleEndian.ReadByte(buffer, offset + 8 + i);
            if (path[componentStart + i] != diskByte)
                return false;
        }

        return true;
    }

    public string ReadName(byte[] buffer, int offset)
    {
        string name = "";
        for (int i = 0; i < NameLength; i++)
        {
            int value = ExtLittleEndian.ReadByte(buffer, offset + 8 + i);
            name = string.Concat(name, ExtText.ByteToString(value));
        }

        return name;
    }

    public int ToNodeKind()
    {
        if (FileType == FileTypeDirectory)
            return 2;
        if (FileType == FileTypeRegularFile || FileType == FileTypeSymlink)
            return 1;
        return 0;
    }
}
