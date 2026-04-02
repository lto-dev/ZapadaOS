using Zapada.Storage;

namespace Zapada.Fs.Fat32;

internal sealed class Fat32Volume : MountedVolume
{
    private const int MaxClusterChain = 128;
    private const int DirEntrySize = 32;
    private const int DirEntriesPerSector = 16;
    private const int AttrLfn = 0x0F;
    private const int AttrVolumeId = 0x08;
    private const int AttrDirectory = 0x10;
    private const int MaxTokens = 8;
    private const int MaxNodes = 64;

    private int _partLba;
    private int _bps;
    private int _spc;
    private int _fatStart;
    private int _dataStart;
    private int _rootCluster;

    private bool[] _nodeUsed = null!;
    private int[] _nodeFirstCluster = null!;
    private int[] _nodeSize = null!;
    private int[] _nodeKind = null!;
    private string[] _nodeName = null!;

    private bool[] _tokenOpen = null!;
    private int[] _tokenNodeHandle = null!;
    private int[] _tokenFirstCluster = null!;
    private int[] _tokenFileSize = null!;
    private int[] _tokenCurrentCluster = null!;
    private int[] _tokenOffsetInCluster = null!;
    private long[] _tokenOffset = null!;

    private int _scanCluster;
    private int _scanSize;
    private int _scanAttr;
    private string _scanName = "";

    public int Initialize(int partLba)
    {
        _partLba = partLba;

        int[] bpb = new int[128];
        if (Zapada.BlockDev.ReadSector((long)partLba, 1, bpb) != 0)
            return StorageStatus.IoError;

        int bps = BufHelper.GetWord(bpb, 11);
        int spc = BufHelper.GetByte(bpb, 13);
        int reserved = BufHelper.GetWord(bpb, 14);
        int fatCount = BufHelper.GetByte(bpb, 16);
        int fatSize16 = BufHelper.GetWord(bpb, 22);
        int fatSize32 = BufHelper.GetDword(bpb, 36);
        int rootCluster = BufHelper.GetDword(bpb, 44);

        if (bps != 512)
            return StorageStatus.NotSupported;

        if (!IsPowerOfTwo(spc) || reserved <= 0 || fatCount <= 0 || rootCluster < 2)
            return StorageStatus.CorruptedData;

        int fatSize = fatSize16 != 0 ? fatSize16 : fatSize32;
        if (fatSize <= 0)
            return StorageStatus.CorruptedData;

        _bps = bps;
        _spc = spc;
        _fatStart = partLba + reserved;
        _dataStart = _fatStart + fatCount * fatSize;
        _rootCluster = rootCluster;

        _nodeUsed = new bool[MaxNodes];
        _nodeFirstCluster = new int[MaxNodes];
        _nodeSize = new int[MaxNodes];
        _nodeKind = new int[MaxNodes];
        _nodeName = new string[MaxNodes];
        _tokenOpen = new bool[MaxTokens];
        _tokenNodeHandle = new int[MaxTokens];
        _tokenFirstCluster = new int[MaxTokens];
        _tokenFileSize = new int[MaxTokens];
        _tokenCurrentCluster = new int[MaxTokens];
        _tokenOffsetInCluster = new int[MaxTokens];
        _tokenOffset = new long[MaxTokens];

        _nodeUsed[0] = true;
        _nodeFirstCluster[0] = _rootCluster;
        _nodeSize[0] = 0;
        _nodeKind[0] = 2;
        _nodeName[0] = "/";

        return StorageStatus.Ok;
    }

    public override string GetDriverKey() { return "fat32"; }
    public override string GetDisplayName() { return "FAT32 volume"; }
    public override string GetVolumeLabel() { return "fat32"; }
    public override string GetVolumeId() { return "fat32"; }

    public override int GetRoot()
    {
        return 0;
    }

    public override int Resolve(string path)
    {
        if (path == null)
            return StorageStatus.InvalidArgument;

        int pathLen = path.Length;
        int pos = 0;
        while (pos < pathLen && path[pos] == '/')
            pos = pos + 1;

        if (pos >= pathLen)
            return 0;

        int currentHandle = 0;
        int currentCluster = _rootCluster;

        while (pos < pathLen)
        {
            int compStart = pos;
            while (pos < pathLen && path[pos] != '/')
                pos = pos + 1;

            int compLen = pos - compStart;
            while (pos < pathLen && path[pos] == '/')
                pos = pos + 1;

            if (compLen <= 0)
                continue;

            int requireDirectory = pos < pathLen ? 1 : 0;
            int rc = FindEntryInDirectory(currentCluster, path, compStart, compLen, requireDirectory);
            if (rc != StorageStatus.Ok)
                return rc;

            currentHandle = CacheScannedNode();
            if (currentHandle < 0)
                return currentHandle;

            currentCluster = _scanCluster;
        }

        return currentHandle;
    }

    public override int Open(int nodeHandle, int accessIntent)
    {
        if (accessIntent != FileAccessIntent.ReadOnly)
            return StorageStatus.NotSupported;

        if (!IsValidNodeHandle(nodeHandle))
            return StorageStatus.NotFound;

        if (_nodeKind[nodeHandle] != 1)
            return StorageStatus.NotFile;

        for (int i = 0; i < MaxTokens; i++)
        {
            if (!_tokenOpen[i])
            {
                _tokenOpen[i] = true;
                _tokenNodeHandle[i] = nodeHandle;
                _tokenFirstCluster[i] = _nodeFirstCluster[nodeHandle];
                _tokenFileSize[i] = _nodeSize[nodeHandle];
                _tokenCurrentCluster[i] = _nodeFirstCluster[nodeHandle];
                _tokenOffsetInCluster[i] = 0;
                _tokenOffset[i] = 0;
                return i;
            }
        }

        return StorageStatus.TableFull;
    }

    public override int Read(int fileToken, byte[] buffer, int offset, int count)
    {
        if (!IsValidToken(fileToken) || buffer == null)
            return StorageStatus.InvalidArgument;

        if (offset < 0 || count < 0 || offset + count > buffer.Length)
            return StorageStatus.InvalidArgument;

        if (count == 0)
            return 0;

        long fileOffset = _tokenOffset[fileToken];
        int fileSize = _tokenFileSize[fileToken];
        if (fileOffset >= fileSize)
            return 0;

        int remaining = fileSize - (int)fileOffset;
        if (count > remaining)
            count = remaining;

        if (_tokenCurrentCluster[fileToken] < 2 && count > 0)
            return StorageStatus.CorruptedData;

        int[] secBuf = new int[128];
        int written = 0;
        int clusterSize = _bps * _spc;

        while (written < count)
        {
            int cluster = _tokenCurrentCluster[fileToken];
            if (cluster < 2)
                return written > 0 ? written : StorageStatus.CorruptedData;

            int clusterOffset = _tokenOffsetInCluster[fileToken];
            int sectorInCluster = clusterOffset / _bps;
            int byteInSector = clusterOffset % _bps;
            int lba = ClusterToLba(cluster) + sectorInCluster;

            if (Zapada.BlockDev.ReadSector((long)lba, 1, secBuf) != 0)
                return written > 0 ? written : StorageStatus.IoError;

            int available = _bps - byteInSector;
            int wanted = count - written;
            if (wanted > available)
                wanted = available;

            for (int i = 0; i < wanted; i++)
                buffer[offset + written + i] = (byte)BufHelper.GetByte(secBuf, byteInSector + i);

            written += wanted;
            _tokenOffset[fileToken] += wanted;
            _tokenOffsetInCluster[fileToken] += wanted;

            if (_tokenOffsetInCluster[fileToken] >= clusterSize && written < count)
            {
                int next = ReadFatEntry(cluster, secBuf);
                if (next < 0)
                    return written > 0 ? written : StorageStatus.IoError;

                if (IsEoc(next))
                {
                    _tokenCurrentCluster[fileToken] = 0;
                    _tokenOffsetInCluster[fileToken] = 0;
                    break;
                }

                _tokenCurrentCluster[fileToken] = next;
                _tokenOffsetInCluster[fileToken] = 0;
            }
        }

        return written;
    }

    public override int Close(int fileToken)
    {
        if (fileToken < 0 || fileToken >= MaxTokens)
            return StorageStatus.InvalidArgument;

        _tokenOpen[fileToken] = false;
        _tokenNodeHandle[fileToken] = 0;
        _tokenFirstCluster[fileToken] = 0;
        _tokenFileSize[fileToken] = 0;
        _tokenCurrentCluster[fileToken] = 0;
        _tokenOffsetInCluster[fileToken] = 0;
        _tokenOffset[fileToken] = 0;
        return StorageStatus.Ok;
    }

    public override int Stat(int nodeHandle, NodeFacts facts)
    {
        if (facts == null)
            return StorageStatus.InvalidArgument;

        if (!IsValidNodeHandle(nodeHandle))
            return StorageStatus.NotFound;

        facts.NodeHandle = nodeHandle;
        facts.NodeKind = _nodeKind[nodeHandle];
        facts.Size = _nodeSize[nodeHandle];
        facts.ModifiedTime = 0;
        facts.Permissions = 0;
        return StorageStatus.Ok;
    }

    public override int ListDirectory(int nodeHandle, DirectoryEntrySink sink)
    {
        if (sink == null)
            return StorageStatus.InvalidArgument;

        if (!IsValidNodeHandle(nodeHandle))
            return StorageStatus.NotFound;

        if (_nodeKind[nodeHandle] != 2)
            return StorageStatus.NotDirectory;

        int dirCluster = _nodeFirstCluster[nodeHandle];
        int[] secBuf = new int[128];
        int cluster = dirCluster;
        int depth = 0;

        while (!IsEoc(cluster) && cluster >= 2 && depth < MaxClusterChain)
        {
            int clusterLba = ClusterToLba(cluster);
            for (int sec = 0; sec < _spc; sec++)
            {
                if (Zapada.BlockDev.ReadSector((long)(clusterLba + sec), 1, secBuf) != 0)
                    return StorageStatus.IoError;

                for (int ent = 0; ent < DirEntriesPerSector; ent++)
                {
                    int entOff = ent * DirEntrySize;
                    int first = BufHelper.GetByte(secBuf, entOff + 0);
                    if (first == 0x00)
                        return StorageStatus.Ok;
                    if (first == 0xE5)
                        continue;

                    int attr = BufHelper.GetByte(secBuf, entOff + 11);
                    if (attr == AttrLfn)
                        continue;
                    if ((attr & AttrVolumeId) != 0)
                        continue;

                    string name = BuildShortName(secBuf, entOff);
                    if (name == "." || name == "..")
                        continue;

                    int childHandle = CacheEntry(secBuf, entOff, attr, name);
                    if (childHandle < 0)
                        return childHandle;

                    sink.OnEntry(childHandle, name, _nodeKind[childHandle]);
                }
            }

            int next = ReadFatEntry(cluster, secBuf);
            if (next < 0)
                return StorageStatus.IoError;
            if (IsEoc(next))
                break;

            cluster = next;
            depth = depth + 1;
        }

        return StorageStatus.Ok;
    }

    public override int Seek(int fileToken, long absoluteOffset)
    {
        if (!IsValidToken(fileToken))
            return StorageStatus.InvalidArgument;

        if (absoluteOffset < 0 || absoluteOffset > _tokenFileSize[fileToken])
            return StorageStatus.InvalidArgument;

        if (_tokenFileSize[fileToken] == 0)
        {
            _tokenOffset[fileToken] = 0;
            _tokenCurrentCluster[fileToken] = 0;
            _tokenOffsetInCluster[fileToken] = 0;
            return StorageStatus.Ok;
        }

        if (absoluteOffset == _tokenFileSize[fileToken])
        {
            _tokenOffset[fileToken] = absoluteOffset;
            _tokenCurrentCluster[fileToken] = _tokenFirstCluster[fileToken];
            _tokenOffsetInCluster[fileToken] = (int)(absoluteOffset % (_bps * _spc));
            return StorageStatus.Ok;
        }

        int clusterSize = _bps * _spc;
        int clusterSteps = (int)(absoluteOffset / clusterSize);
        int cluster = _tokenFirstCluster[fileToken];
        int[] fatBuf = new int[128];

        for (int i = 0; i < clusterSteps; i++)
        {
            int next = ReadFatEntry(cluster, fatBuf);
            if (next < 0 || IsEoc(next) || next < 2)
                return StorageStatus.CorruptedData;
            cluster = next;
        }

        _tokenOffset[fileToken] = absoluteOffset;
        _tokenCurrentCluster[fileToken] = cluster;
        _tokenOffsetInCluster[fileToken] = (int)(absoluteOffset % clusterSize);
        return StorageStatus.Ok;
    }

    private bool IsValidToken(int fileToken)
    {
        return fileToken >= 0 && fileToken < MaxTokens && _tokenOpen[fileToken];
    }

    private bool IsValidNodeHandle(int nodeHandle)
    {
        return nodeHandle >= 0 && nodeHandle < MaxNodes && _nodeUsed[nodeHandle];
    }

    private int CacheScannedNode()
    {
        return CacheNode(_scanCluster, _scanSize, (_scanAttr & AttrDirectory) != 0 ? 2 : 1, _scanName);
    }

    private int CacheEntry(int[] secBuf, int entOff, int attr, string name)
    {
        int clHi = BufHelper.GetWord(secBuf, entOff + 20);
        int clLo = BufHelper.GetWord(secBuf, entOff + 26);
        int cluster = (clHi << 16) | clLo;
        int size = BufHelper.GetDword(secBuf, entOff + 28);
        int kind = (attr & AttrDirectory) != 0 ? 2 : 1;
        return CacheNode(cluster, size, kind, name);
    }

    private int CacheNode(int firstCluster, int size, int kind, string name)
    {
        for (int i = 0; i < MaxNodes; i++)
        {
            if (_nodeUsed[i]
                && _nodeFirstCluster[i] == firstCluster
                && _nodeSize[i] == size
                && _nodeKind[i] == kind
                && _nodeName[i] == name)
            {
                return i;
            }
        }

        for (int i = 1; i < MaxNodes; i++)
        {
            if (!_nodeUsed[i])
            {
                _nodeUsed[i] = true;
                _nodeFirstCluster[i] = firstCluster;
                _nodeSize[i] = size;
                _nodeKind[i] = kind;
                _nodeName[i] = name;
                return i;
            }
        }

        return StorageStatus.TableFull;
    }

    private int FindEntryInDirectory(int dirCluster, string path, int compStart, int compLen, int requireDirectory)
    {
        int[] secBuf = new int[128];
        int cluster = dirCluster;
        int depth = 0;

        while (!IsEoc(cluster) && cluster >= 2 && depth < MaxClusterChain)
        {
            int clusterLba = ClusterToLba(cluster);
            for (int sec = 0; sec < _spc; sec++)
            {
                if (Zapada.BlockDev.ReadSector((long)(clusterLba + sec), 1, secBuf) != 0)
                    return StorageStatus.IoError;

                for (int ent = 0; ent < DirEntriesPerSector; ent++)
                {
                    int entOff = ent * DirEntrySize;
                    int first = BufHelper.GetByte(secBuf, entOff + 0);
                    if (first == 0x00)
                        return StorageStatus.NotFound;
                    if (first == 0xE5)
                        continue;

                    int attr = BufHelper.GetByte(secBuf, entOff + 11);
                    if (attr == AttrLfn)
                        continue;

                    if (!MatchPathComponent83(path, compStart, compLen, secBuf, entOff))
                        continue;

                    if (requireDirectory != 0 && (attr & AttrDirectory) == 0)
                        return StorageStatus.NotDirectory;

                    int clHi = BufHelper.GetWord(secBuf, entOff + 20);
                    int clLo = BufHelper.GetWord(secBuf, entOff + 26);
                    _scanCluster = (clHi << 16) | clLo;
                    _scanSize = BufHelper.GetDword(secBuf, entOff + 28);
                    _scanAttr = attr;
                    _scanName = BuildShortName(secBuf, entOff);
                    return StorageStatus.Ok;
                }
            }

            int next = ReadFatEntry(cluster, secBuf);
            if (next < 0)
                return StorageStatus.IoError;
            if (IsEoc(next))
                break;

            cluster = next;
            depth = depth + 1;
        }

        return StorageStatus.NotFound;
    }

    private bool MatchPathComponent83(string path, int compStart, int compLen, int[] buf, int entOff)
    {
        int dot = -1;
        for (int i = 0; i < compLen; i++)
        {
            if (path[compStart + i] == '.')
            {
                if (dot >= 0)
                    return false;
                dot = i;
            }
        }

        int baseLen = dot >= 0 ? dot : compLen;
        int extLen = dot >= 0 ? (compLen - dot - 1) : 0;
        if (baseLen <= 0 || baseLen > 8 || extLen > 3)
            return false;

        for (int i = 0; i < 8; i++)
        {
            int want = 0x20;
            if (i < baseLen)
                want = ToUpperAscii(path[compStart + i]);
            if (BufHelper.GetByte(buf, entOff + i) != want)
                return false;
        }

        for (int i = 0; i < 3; i++)
        {
            int want = 0x20;
            if (i < extLen)
                want = ToUpperAscii(path[compStart + baseLen + 1 + i]);
            if (BufHelper.GetByte(buf, entOff + 8 + i) != want)
                return false;
        }

        return true;
    }

    private string BuildShortName(int[] buf, int entOff)
    {
        string baseName = "";
        for (int i = 0; i < 8; i++)
        {
            int ch = BufHelper.GetByte(buf, entOff + i);
            if (ch == 0x20)
                break;
            baseName = string.Concat(baseName, ByteToString(ch));
        }

        string ext = "";
        for (int i = 0; i < 3; i++)
        {
            int ch = BufHelper.GetByte(buf, entOff + 8 + i);
            if (ch == 0x20)
                break;
            ext = string.Concat(ext, ByteToString(ch));
        }

        if (ext.Length > 0)
            return string.Concat(string.Concat(baseName, "."), ext);

        return baseName;
    }

    private string ByteToString(int value)
    {
        if (value >= 0x61 && value <= 0x7A)
            return ByteToString(value - 0x20);

        if (value == 0x20) return " ";
        if (value == 0x21) return "!";
        if (value == 0x23) return "#";
        if (value == 0x24) return "$";
        if (value == 0x25) return "%";
        if (value == 0x26) return "&";
        if (value == 0x27) return "'";
        if (value == 0x28) return "(";
        if (value == 0x29) return ")";
        if (value == 0x2D) return "-";
        if (value == 0x2E) return ".";
        if (value == 0x30) return "0";
        if (value == 0x31) return "1";
        if (value == 0x32) return "2";
        if (value == 0x33) return "3";
        if (value == 0x34) return "4";
        if (value == 0x35) return "5";
        if (value == 0x36) return "6";
        if (value == 0x37) return "7";
        if (value == 0x38) return "8";
        if (value == 0x39) return "9";
        if (value == 0x40) return "@";
        if (value == 0x41) return "A";
        if (value == 0x42) return "B";
        if (value == 0x43) return "C";
        if (value == 0x44) return "D";
        if (value == 0x45) return "E";
        if (value == 0x46) return "F";
        if (value == 0x47) return "G";
        if (value == 0x48) return "H";
        if (value == 0x49) return "I";
        if (value == 0x4A) return "J";
        if (value == 0x4B) return "K";
        if (value == 0x4C) return "L";
        if (value == 0x4D) return "M";
        if (value == 0x4E) return "N";
        if (value == 0x4F) return "O";
        if (value == 0x50) return "P";
        if (value == 0x51) return "Q";
        if (value == 0x52) return "R";
        if (value == 0x53) return "S";
        if (value == 0x54) return "T";
        if (value == 0x55) return "U";
        if (value == 0x56) return "V";
        if (value == 0x57) return "W";
        if (value == 0x58) return "X";
        if (value == 0x59) return "Y";
        if (value == 0x5A) return "Z";
        if (value == 0x5E) return "^";
        if (value == 0x5F) return "_";
        if (value == 0x60) return "`";
        if (value == 0x7B) return "{";
        if (value == 0x7D) return "}";
        if (value == 0x7E) return "~";

        return "?";
    }

    private int ClusterToLba(int cluster)
    {
        return _dataStart + (cluster - 2) * _spc;
    }

    private int ReadFatEntry(int cluster, int[] secBuf)
    {
        int byteOff = cluster * 4;
        int fatSec = _fatStart + (byteOff / _bps);
        int inSec = byteOff % _bps;
        if (Zapada.BlockDev.ReadSector((long)fatSec, 1, secBuf) != 0)
            return -1;
        return BufHelper.GetDword(secBuf, inSec) & 0x0FFFFFFF;
    }

    private bool IsEoc(int entry)
    {
        return (entry & 0x0FFFFFFF) >= 0x0FFFFFF8;
    }

    private bool IsPowerOfTwo(int value)
    {
        if (value <= 0)
            return false;
        return (value & (value - 1)) == 0;
    }

    private int ToUpperAscii(int ch)
    {
        if (ch >= 'a' && ch <= 'z')
            return ch - 32;
        return ch;
    }
}
