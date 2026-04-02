/*
 * Zapada - src/managed/Zapada.Fs.Fat32/Fat32Reader.cs
 *
 * Phase 3.3: Managed FAT32 filesystem reader — full implementation.
 *
 * Provides real BPB-based volume initialization, root directory scan for
 * CONF.DLL, and cluster-chain file reading for use by the VFS layer.
 *
 * Phase 3.1 D.3 placeholder replaced by:
 *   - Initialize(): reads BPB from boot partition via GetBootPartLba()
 *   - FindFile(path): locates a file in the root directory by path
 *   - ReadFile(...): reads file bytes by following the FAT32 cluster chain
 *
 * FAT32 BPB field offsets in the partition boot sector:
 *   11-12 (word):  BytesPerSector    (expect 512)
 *   13    (byte):  SectorsPerCluster
 *   14-15 (word):  ReservedSectors
 *   16    (byte):  FATCount          (usually 2)
 *   36-39 (dword): FATSize32         (sectors per FAT)
 *   44-47 (dword): RootCluster       (first cluster of root directory)
 *
 * FAT32 directory entry layout (32 bytes per entry):
 *   0-7:   8.3 name (space-padded uppercase, 0x00=end, 0xE5=deleted)
 *   8-10:  Extension (3 chars, space-padded)
 *   11:    Attributes (0x0F = LFN, skip)
 *   20-21: FirstClusterHigh (word)
 *   26-27: FirstClusterLow  (word)
 *   28-31: FileSize (dword)
 *
 * AArch64 alignment: all accesses use BufHelper's byte-granular int[] reads.
 *
 * Namespace: Zapada.Fs.Fat32
 */

using System;

namespace Zapada.Fs.Fat32
{
    /*
     * Fat32Reader - static class for managed FAT32 volume access.
     *
     * Made public so Zapada.Fs.Vfs can call FindFile() and ReadFile()
     * via a cross-assembly call after FAT32.DLL is loaded.
     */
    public static class Fat32Reader
    {
        /* Maximum cluster chain length to follow (prevents infinite loops). */
        private const int MAX_CLUSTER_CHAIN = 128;

        /* Directory entries per 512-byte sector. */
        private const int DIR_ENTRIES_PER_SECTOR = 16;

        /* Directory entry size in bytes. */
        private const int DIR_ENTRY_SIZE = 32;

        /* LFN attribute byte value (skip these entries). */
        private const int ATTR_LFN = 0x0F;

        /* Directory attribute byte value. */
        private const int ATTR_DIRECTORY = 0x10;

        /* FAT32 LFN constants. */
        private const int LFN_CHARS_PER_ENTRY = 13;
        private const int LFN_END_CHAR = 0x0000;
        private const int LFN_PAD_CHAR = 0xFFFF;

        /* ------------------------------------------------------------------ */
        /* Volume state (populated by Initialize)                              */
        /* ------------------------------------------------------------------ */

        private static int s_initialized = 0;
        private static int s_bps         = 0;   /* bytes per sector            */
        private static int s_spc         = 0;   /* sectors per cluster         */
        private static int s_fatStart    = 0;   /* FAT1 start LBA              */
        private static int s_dataStart   = 0;   /* data area start LBA         */
        private static int s_rootCluster = 0;   /* root directory first cluster */

        /* ------------------------------------------------------------------ */
        /* Last FindFile result (set by FindConfDll, read by VFS Open)         */
        /* ------------------------------------------------------------------ */

        /*
         * s_lastCluster / s_lastSize - private backing fields set by FindConfDll.
         * Exposed through explicit getter methods so VFS can retrieve them via
         * cross-assembly call instructions (avoids cross-assembly ldsfld which
         * requires MemberRef field token resolution).
         */
        private static int s_lastCluster = 0;
        private static int s_lastSize    = 0;

        /*
         * GetLastCluster() - returns the first cluster of the most recently
         * found file.  Returns 0 before any successful FindFile() call.
         */
        public static int GetLastCluster()
        {
            return s_lastCluster;
        }

        /*
         * GetLastSize() - returns the file size in bytes of the most recently
         * found file.  Returns 0 before any successful FindFile() call.
         */
        public static int GetLastSize()
        {
            return s_lastSize;
        }

        /* ------------------------------------------------------------------ */
        /* Cluster chain helpers                                               */
        /* ------------------------------------------------------------------ */

        private static int ClusterToLba(int cluster)
        {
            return s_dataStart + (cluster - 2) * s_spc;
        }

        /*
         * ReadFatEntry - read the 32-bit FAT entry for <cluster>.
         *
         * Overwrites secBuf with the FAT sector containing the entry.
         * Returns the masked entry value (high nibble cleared per ECMA spec)
         * on success, or -1 on I/O error.
         */
        private static int ReadFatEntry(int cluster, int[] secBuf)
        {
            int byteOff = cluster * 4;
            int fatSec  = s_fatStart + byteOff / s_bps;
            int inSec   = byteOff % s_bps;

            if (Zapada.BlockDev.ReadSector((long)fatSec, 1, secBuf) != 0)
            {
                return -1;
            }

            int raw = BufHelper.GetDword(secBuf, inSec);
            return raw & 0x0FFFFFFF;
        }

        private static bool IsEoc(int entry)
        {
            /*
             * FAT32 end-of-chain values are 0x0FFFFFF8..0x0FFFFFFF.
             * 0x0FFFFFF8 = 268435448, which fits in a signed int32.
             */
            return (entry & 0x0FFFFFFF) >= 0x0FFFFFF8;
        }

        /* ------------------------------------------------------------------ */
        /* Initialize - parse BPB and set up volume state                      */
        /* ------------------------------------------------------------------ */

        /*
         * Initialize() - read the FAT32 BPB from the boot partition and
         * populate the static volume fields.
         *
         * Calls Zapada.Kernel.GetBootPartLba() to retrieve the partition start LBA
         * stored by Zapada.Boot.BootLoader after the boot-time Fat32.Mount() call.
         *
         * Returns 0 on success, -1 on I/O error or invalid BPB.
         */
        public static int Initialize()
        {
            s_initialized = 0;

            int partLba = Zapada.Kernel.GetBootPartLba();
            if (partLba <= 0)
            {
                Console.Write("[Fat32] boot part LBA not set\n");
                return -1;
            }

            int[] bpb = new int[128];   /* 512-byte BPB sector */

            if (Zapada.BlockDev.ReadSector((long)partLba, 1, bpb) != 0)
            {
                Console.Write("[Fat32] I/O error reading BPB\n");
                return -1;
            }

            int bps         = BufHelper.GetWord(bpb, 11);   /* BytesPerSector    */
            int spc         = BufHelper.GetByte(bpb, 13);   /* SectorsPerCluster */
            int reserved    = BufHelper.GetWord(bpb, 14);   /* ReservedSectors   */
            int fatCount    = BufHelper.GetByte(bpb, 16);   /* FATCount          */
            int fatSize16   = BufHelper.GetWord(bpb, 22);   /* FATSize16         */
            int fatSize32   = BufHelper.GetDword(bpb, 36);  /* FATSize32         */
            int rootCluster = BufHelper.GetDword(bpb, 44);  /* RootCluster       */

            if (bps != 512)
            {
                Console.Write("[Fat32] unsupported sector size\n");
                return -1;
            }

            int fatSize = (fatSize16 != 0) ? fatSize16 : fatSize32;
            if (fatSize == 0 || spc == 0)
            {
                Console.Write("[Fat32] invalid BPB\n");
                return -1;
            }

            s_bps         = bps;
            s_spc         = spc;
            s_fatStart    = partLba + reserved;
            s_dataStart   = s_fatStart + fatCount * fatSize;
            s_rootCluster = rootCluster;
            s_initialized = 1;

            return 0;
        }

        /*
         * IsInitialized() - returns 1 when Initialize() succeeded.
         */
        public static int IsInitialized()
        {
            return s_initialized;
        }

        /* ------------------------------------------------------------------ */
        /* Name matching helpers                                               */
        /* ------------------------------------------------------------------ */

        /*
         * IsConfDll - returns true when the 11-byte 8.3 name at entOff in buf
         * equals "CONF    DLL" (4 chars + 4 spaces + extension "DLL").
         *
         * Corresponds to the file CONF.DLL on a FAT32 volume.
         *
         * Byte values:
         *   [0]='C'=0x43  [1]='O'=0x4F  [2]='N'=0x4E  [3]='F'=0x46
         *   [4]=0x20  [5]=0x20  [6]=0x20  [7]=0x20
         *   [8]='D'=0x44  [9]='L'=0x4C  [10]='L'=0x4C
         */
        private static bool IsConfDll(int[] buf, int entOff)
        {
            if (BufHelper.GetByte(buf, entOff +  0) != 0x43) return false; /* C */
            if (BufHelper.GetByte(buf, entOff +  1) != 0x4F) return false; /* O */
            if (BufHelper.GetByte(buf, entOff +  2) != 0x4E) return false; /* N */
            if (BufHelper.GetByte(buf, entOff +  3) != 0x46) return false; /* F */
            if (BufHelper.GetByte(buf, entOff +  4) != 0x20) return false; /* SPC */
            if (BufHelper.GetByte(buf, entOff +  5) != 0x20) return false; /* SPC */
            if (BufHelper.GetByte(buf, entOff +  6) != 0x20) return false; /* SPC */
            if (BufHelper.GetByte(buf, entOff +  7) != 0x20) return false; /* SPC */
            if (BufHelper.GetByte(buf, entOff +  8) != 0x44) return false; /* D */
            if (BufHelper.GetByte(buf, entOff +  9) != 0x4C) return false; /* L */
            if (BufHelper.GetByte(buf, entOff + 10) != 0x4C) return false; /* L */
            return true;
        }

        /* ------------------------------------------------------------------ */
        /* Root-directory name matching                                        */
        /* ------------------------------------------------------------------ */

        private static int s_matchCluster = 0;
        private static int s_matchSize    = 0;
        private static int s_matchAttr    = 0;

        private static bool MatchEntry83Literal(int[] buf, int entOff, string name83)
        {
            int i = 0;
            while (i < 11)
            {
                if (BufHelper.GetByte(buf, entOff + i) != name83[i])
                {
                    return false;
                }

                i = i + 1;
            }

            return true;
        }

        private static void DebugWriteEntryName(int[] buf, int entOff, int attr)
        {
            if (MatchEntry83Literal(buf, entOff, "ZAPADA_BOOT "))
            {
                Console.Write("ZAPADA_BOOT");
            }
            else if (MatchEntry83Literal(buf, entOff, "TEST    DLL"))
            {
                Console.Write("TEST.DLL");
            }
            else if (MatchEntry83Literal(buf, entOff, "VBLK    DLL"))
            {
                Console.Write("VBLK.DLL");
            }
            else if (MatchEntry83Literal(buf, entOff, "GPT     DLL"))
            {
                Console.Write("GPT.DLL");
            }
            else if (MatchEntry83Literal(buf, entOff, "FAT32   DLL"))
            {
                Console.Write("FAT32.DLL");
            }
            else if (MatchEntry83Literal(buf, entOff, "VFS     DLL"))
            {
                Console.Write("VFS.DLL");
            }
            else if (MatchEntry83Literal(buf, entOff, "CONF    DLL"))
            {
                Console.Write("CONF.DLL");
            }
            else if (MatchEntry83Literal(buf, entOff, "SYS        "))
            {
                Console.Write("SYS");
            }
            else
            {
                Console.Write("<unknown-83>");
            }

            if ((attr & ATTR_DIRECTORY) != 0)
            {
                Console.Write(" <DIR>");
            }
        }

        private static bool MatchPathComponent83(string path, int compStart, int compLen,
                                                 int[] buf, int entOff)
        {
            int i = 0;

            if (compLen <= 0)
            {
                return false;
            }

            int dot = -1;
            while (i < compLen)
            {
                if (path[compStart + i] == '.')
                {
                    if (dot >= 0)
                    {
                        return false;
                    }

                    dot = i;
                }

                i = i + 1;
            }

            int baseLen = (dot >= 0) ? dot : compLen;
            int extLen  = (dot >= 0) ? (compLen - dot - 1) : 0;
            if (baseLen <= 0 || baseLen > 8 || extLen > 3)
            {
                return false;
            }

            i = 0;
            while (i < 8)
            {
                int want = 0x20;
                if (i < baseLen)
                {
                    want = path[compStart + i];
                }

                if (BufHelper.GetByte(buf, entOff + i) != want)
                {
                    return false;
                }

                i = i + 1;
            }

            i = 0;
            while (i < 3)
            {
                int want = 0x20;
                if (i < extLen)
                {
                    want = path[compStart + baseLen + 1 + i];
                }

                if (BufHelper.GetByte(buf, entOff + 8 + i) != want)
                {
                    return false;
                }

                i = i + 1;
            }

            return true;
        }

        private static int ReadLfnChar(int[] buf, int entOff, int charIndex)
        {
            if (charIndex < 5)
            {
                return BufHelper.GetWord(buf, entOff + 1 + charIndex * 2);
            }

            if (charIndex < 11)
            {
                return BufHelper.GetWord(buf, entOff + 14 + (charIndex - 5) * 2);
            }

            return BufHelper.GetWord(buf, entOff + 28 + (charIndex - 11) * 2);
        }

        private static int ExtractLfnFragment(int[] buf, int entOff, int[] outChars, int outStart)
        {
            int written = 0;
            int i = 0;

            while (i < LFN_CHARS_PER_ENTRY)
            {
                if (outStart + written >= outChars.Length)
                {
                    return -1;
                }

                int ch = ReadLfnChar(buf, entOff, i);
                if (ch == LFN_END_CHAR || ch == LFN_PAD_CHAR)
                {
                    break;
                }

                outChars[outStart + written] = ch;
                written = written + 1;
                i = i + 1;
            }

            return written;
        }

        private static bool MatchPathComponentLfn(string path, int compStart, int compLen,
                                                  int[] lfnChars, int lfnLen)
        {
            if (lfnLen != compLen)
            {
                return false;
            }

            int i = 0;
            while (i < compLen)
            {
                if (lfnChars[i] != path[compStart + i])
                {
                    return false;
                }

                i = i + 1;
            }

            return true;
        }

        private static int FindEntryInDirectory(int dirCluster, string path, int compStart,
                                                int compLen, int requireDirectory)
        {
            int[] secBuf  = new int[128];
            int   cluster = dirCluster;
            int   depth   = 0;
            int[] lfnChars = new int[260];
            int   lfnLen = 0;

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN)
            {
                int clusterLba = ClusterToLba(cluster);
                int sec        = 0;

                while (sec < s_spc)
                {
                    if (Zapada.BlockDev.ReadSector((long)(clusterLba + sec), 1, secBuf) != 0)
                    {
                        return -1;
                    }

                    int ent = 0;
                    while (ent < DIR_ENTRIES_PER_SECTOR)
                    {
                        int entOff = ent * DIR_ENTRY_SIZE;
                        int first  = BufHelper.GetByte(secBuf, entOff + 0);

                        if (first == 0x00)
                        {
                            return -1;
                        }

                        if (first == 0xE5)
                        {
                            lfnLen = 0;
                            ent = ent + 1;
                            continue;
                        }

                        {
                            int attr = BufHelper.GetByte(secBuf, entOff + 11);
                            if (attr == ATTR_LFN)
                            {
                                int seq = BufHelper.GetByte(secBuf, entOff + 0) & 0x1F;
                                int destStart;
                                int fragLen;

                                if (seq == 0)
                                {
                                    lfnLen = 0;
                                    ent = ent + 1;
                                    continue;
                                }

                                destStart = (seq - 1) * LFN_CHARS_PER_ENTRY;
                                if (destStart < 0 || destStart >= lfnChars.Length)
                                {
                                    lfnLen = 0;
                                    ent = ent + 1;
                                    continue;
                                }

                                fragLen = ExtractLfnFragment(secBuf, entOff, lfnChars, destStart);
                                if (fragLen < 0 || destStart + fragLen > lfnChars.Length)
                                {
                                    lfnLen = 0;
                                    ent = ent + 1;
                                    continue;
                                }

                                if (destStart + fragLen > lfnLen)
                                {
                                    lfnLen = destStart + fragLen;
                                }

                                ent = ent + 1;
                                continue;
                            }
                        }

                        if (first != 0xE5)
                        {
                            int attr = BufHelper.GetByte(secBuf, entOff + 11);
                            if ((lfnLen > 0 && MatchPathComponentLfn(path, compStart, compLen, lfnChars, lfnLen))
                                || MatchPathComponent83(path, compStart, compLen, secBuf, entOff))
                            {
                                if (requireDirectory != 0 && (attr & ATTR_DIRECTORY) == 0)
                                {
                                    return -1;
                                }

                                int clHi = BufHelper.GetWord(secBuf, entOff + 20);
                                int clLo = BufHelper.GetWord(secBuf, entOff + 26);
                                s_matchCluster = (clHi << 16) | clLo;
                                s_matchSize    = BufHelper.GetDword(secBuf, entOff + 28);
                                s_matchAttr    = attr;
                                return 0;
                            }

                            lfnLen = 0;
                        }

                        ent = ent + 1;
                    }

                    sec = sec + 1;
                }

                int next = ReadFatEntry(cluster, secBuf);
                if (next < 0)
                {
                    return -1;
                }

                cluster = next;
                depth   = depth + 1;
            }

            return -1;
        }

        /*
         * MatchRoot83Path(path, buf, entOff) - compare the final component of
         * an absolute VFS path against one FAT32 root directory 8.3 entry.
         *
         * Current Phase 3.3 scope:
         * - absolute paths only
         * - root-directory files only
         * - one optional '.' separator
         * - exact case-sensitive match against the stored uppercase 8.3 name
         */
        private static bool MatchRoot83Path(string path, int[] buf, int entOff)
        {
            int pathLen   = path.Length;
            int lastSlash = -1;
            int i         = 0;

            while (i < pathLen)
            {
                if (path[i] == '/')
                {
                    lastSlash = i;
                }
                i = i + 1;
            }

            int nameStart = lastSlash + 1;
            int nameLen   = pathLen - nameStart;
            if (nameLen <= 0)
            {
                return false;
            }

            int dot = -1;
            i = 0;
            while (i < nameLen)
            {
                if (path[nameStart + i] == '.')
                {
                    if (dot >= 0)
                    {
                        return false;
                    }
                    dot = i;
                }
                i = i + 1;
            }

            int baseLen = (dot >= 0) ? dot : nameLen;
            int extLen  = (dot >= 0) ? (nameLen - dot - 1) : 0;

            if (baseLen <= 0 || baseLen > 8 || extLen > 3)
            {
                return false;
            }

            i = 0;
            while (i < 8)
            {
                int want = 0x20;
                if (i < baseLen)
                {
                    want = path[nameStart + i];
                }

                if (BufHelper.GetByte(buf, entOff + i) != want)
                {
                    return false;
                }

                i = i + 1;
            }

            i = 0;
            while (i < 3)
            {
                int want = 0x20;
                if (i < extLen)
                {
                    want = path[nameStart + baseLen + 1 + i];
                }

                if (BufHelper.GetByte(buf, entOff + 8 + i) != want)
                {
                    return false;
                }

                i = i + 1;
            }

            return true;
        }

        /* ------------------------------------------------------------------ */
        /* FindConfDll - scan root directory for CONF.DLL                      */
        /* ------------------------------------------------------------------ */

        /*
         * FindConfDll() - scan the FAT32 root directory cluster chain for an
         * entry whose 8.3 name equals "CONF    DLL".
         *
         * On success sets LastCluster and LastSize, returns 0.
         * Returns -1 when the file is not found or on I/O error.
         */
        private static int FindConfDll()
        {
            int[] secBuf  = new int[128];
            int   cluster = s_rootCluster;
            int   depth   = 0;

            if (s_rootCluster < 2)
            {
                return -1;
            }
            if (s_spc <= 0)
            {
                return -1;
            }

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN)
            {
                int clusterLba = ClusterToLba(cluster);
                int sec        = 0;

                while (sec < s_spc)
                {
                    int iorc = Zapada.BlockDev.ReadSector((long)(clusterLba + sec), 1, secBuf);
                    if (iorc != 0)
                    {
                        return -1;
                    }

                    int ent = 0;
                    while (ent < DIR_ENTRIES_PER_SECTOR)
                    {
                        int entOff = ent * DIR_ENTRY_SIZE;
                        int first  = BufHelper.GetByte(secBuf, entOff + 0);

                        /* 0x00 = end of directory entries. */
                        if (first == 0x00)
                        {
                            return -1;
                        }

                        /* 0xE5 = deleted; 0x0F = LFN; skip both. */
                        if (first != 0xE5)
                        {
                            int attr = BufHelper.GetByte(secBuf, entOff + 11);
                            if (attr != ATTR_LFN)
                            {
                                if (IsConfDll(secBuf, entOff))
                                {
                                    int clHi     = BufHelper.GetWord(secBuf, entOff + 20);
                                    int clLo     = BufHelper.GetWord(secBuf, entOff + 26);
                                    s_lastCluster = (clHi << 16) | clLo;
                                    s_lastSize    = BufHelper.GetDword(secBuf, entOff + 28);
                                    return 0;
                                }
                            }
                        }

                        ent = ent + 1;
                    }
                    sec = sec + 1;
                }

                /* Advance to next cluster in the directory chain. */
                int next = ReadFatEntry(cluster, secBuf);
                if (next < 0)
                {
                    return -1;
                }
                cluster = next;
                depth   = depth + 1;
            }

            return -1;
        }

        /* ------------------------------------------------------------------ */
        /* FindFile - locate a file by VFS path                                */
        /* ------------------------------------------------------------------ */

        /*
         * FindFile(path) - locate a file by VFS path.
         *
         * Current Phase 3.3 scope:
         * - root-directory 8.3 files
         * - one nested directory level under the root (for example `SYS/CONF.DLL`)
         * - exact uppercase 8.3 names only
         */
        public static int FindFile(string path)
        {
            if (s_initialized == 0)
            {
                return -1;
            }

            Console.Write("[Fat32] FindFile path=");
            Console.Write(path);
            Console.Write(" len=");
            Console.Write(path.Length);
            Console.Write("\n");

            if (s_rootCluster < 2)
            {
                return -1;
            }
            if (s_spc <= 0)
            {
                return -1;
            }

            int pathLen = path.Length;
            int pos = 0;
            while (pos < pathLen && path[pos] == '/')
            {
                pos = pos + 1;
            }

            if (pos >= pathLen)
            {
                return -1;
            }

            int firstStart = pos;
            while (pos < pathLen && path[pos] != '/')
            {
                pos = pos + 1;
            }

            int firstLen = pos - firstStart;

            if (firstLen == 4
                && path[firstStart + 0] == 'b'
                && path[firstStart + 1] == 'o'
                && path[firstStart + 2] == 'o'
                && path[firstStart + 3] == 't')
            {
                while (pos < pathLen && path[pos] == '/')
                {
                    pos = pos + 1;
                }

                if (pos >= pathLen)
                {
                    return -1;
                }

                firstStart = pos;
                while (pos < pathLen && path[pos] != '/')
                {
                    pos = pos + 1;
                }

                firstLen = pos - firstStart;
            }

            while (pos < pathLen && path[pos] == '/')
            {
                pos = pos + 1;
            }

            if (pos >= pathLen)
            {
                Console.Write("[Fat32] FindFile root-only\n");
                if (FindEntryInDirectory(s_rootCluster, path, firstStart, firstLen, 0) != 0)
                {
                    return -1;
                }

                s_lastCluster = s_matchCluster;
                s_lastSize = s_matchSize;
                return 0;
            }

            int secondStart = pos;
            while (pos < pathLen && path[pos] != '/')
            {
                pos = pos + 1;
            }

            int secondLen = pos - secondStart;
            while (pos < pathLen && path[pos] == '/')
            {
                pos = pos + 1;
            }

            if (pos < pathLen)
            {
                Console.Write("[Fat32] FindFile too deep\n");
                return -1;
            }

            Console.Write("[Fat32] FindFile nested\n");

            if (FindEntryInDirectory(s_rootCluster, path, firstStart, firstLen, 1) != 0)
            {
                return -1;
            }

            if (FindEntryInDirectory(s_matchCluster, path, secondStart, secondLen, 0) != 0)
            {
                return -1;
            }

            s_lastCluster = s_matchCluster;
            s_lastSize = s_matchSize;
            return 0;
        }

        /* ------------------------------------------------------------------ */
        /* Debug directory listing                                              */
        /* ------------------------------------------------------------------ */

        public static void DebugListRoot()
        {
            int[] secBuf  = new int[128];
            int   cluster = s_rootCluster;
            int   depth   = 0;

            if (s_initialized == 0 || s_rootCluster < 2 || s_spc <= 0)
            {
                Console.Write("[Fat32] root list unavailable\n");
                return;
            }

            Console.Write("[Fat32] root dir listing\n");

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN)
            {
                int clusterLba = ClusterToLba(cluster);
                int sec        = 0;

                while (sec < s_spc)
                {
                    if (Zapada.BlockDev.ReadSector((long)(clusterLba + sec), 1, secBuf) != 0)
                    {
                        Console.Write("[Fat32] root list I/O error\n");
                        return;
                    }

                    int ent = 0;
                    while (ent < DIR_ENTRIES_PER_SECTOR)
                    {
                        int entOff = ent * DIR_ENTRY_SIZE;
                        int first  = BufHelper.GetByte(secBuf, entOff + 0);

                        if (first == 0x00)
                        {
                            return;
                        }

                        if (first != 0xE5)
                        {
                            int attr = BufHelper.GetByte(secBuf, entOff + 11);
                            if (attr != ATTR_LFN)
                            {
                                Console.Write("[Fat32] entry ");
                                DebugWriteEntryName(secBuf, entOff, attr);

                                Console.Write("\n");
                            }
                        }

                        ent = ent + 1;
                    }

                    sec = sec + 1;
                }

                int next = ReadFatEntry(cluster, secBuf);
                if (next < 0)
                {
                    Console.Write("[Fat32] root list FAT error\n");
                    return;
                }

                cluster = next;
                depth   = depth + 1;
            }
        }

        public static int ListDirectory(string path)
        {
            if (s_initialized == 0 || s_rootCluster < 2 || s_spc <= 0)
            {
                return -1;
            }

            if (path == null || path.Length == 0 || path == "/")
            {
                DebugListRoot();
                return 0;
            }

            if (path == "/SYS")
            {
                DebugListSys();
                return 0;
            }

            return -1;
        }

        public static void DebugListSys()
        {
            int[] secBuf = new int[128];
            int   cluster;
            int   depth;

            if (s_initialized == 0 || s_rootCluster < 2 || s_spc <= 0)
            {
                Console.Write("[Fat32] SYS list unavailable\n");
                return;
            }

            if (FindEntryInDirectory(s_rootCluster, "SYS", 0, 3, 1) != 0)
            {
                Console.Write("[Fat32] SYS directory not found\n");
                return;
            }

            cluster = s_matchCluster;
            depth   = 0;
            Console.Write("[Fat32] SYS dir listing\n");

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN)
            {
                int clusterLba = ClusterToLba(cluster);
                int sec        = 0;

                while (sec < s_spc)
                {
                    if (Zapada.BlockDev.ReadSector((long)(clusterLba + sec), 1, secBuf) != 0)
                    {
                        Console.Write("[Fat32] SYS list I/O error\n");
                        return;
                    }

                    int ent = 0;
                    while (ent < DIR_ENTRIES_PER_SECTOR)
                    {
                        int entOff = ent * DIR_ENTRY_SIZE;
                        int first  = BufHelper.GetByte(secBuf, entOff + 0);

                        if (first == 0x00)
                        {
                            return;
                        }

                        if (first != 0xE5)
                        {
                            int attr = BufHelper.GetByte(secBuf, entOff + 11);
                            if (attr != ATTR_LFN)
                            {
                                Console.Write("[Fat32] sys entry ");
                                DebugWriteEntryName(secBuf, entOff, attr);
                                Console.Write("\n");
                            }
                        }

                        ent = ent + 1;
                    }

                    sec = sec + 1;
                }

                int next = ReadFatEntry(cluster, secBuf);
                if (next < 0)
                {
                    Console.Write("[Fat32] SYS list FAT error\n");
                    return;
                }

                cluster = next;
                depth   = depth + 1;
            }
        }

        /* ------------------------------------------------------------------ */
        /* ReadFile - read bytes from a file's cluster chain into a byte[]     */
        /* ------------------------------------------------------------------ */

        /*
         * ReadFile(startCluster, totalSize, buf, offset, count)
         *
         * Reads up to <count> bytes starting at the beginning of the file
         * identified by <startCluster> and <totalSize>, writing into <buf>
         * starting at <offset>.
         *
         * Returns the number of bytes written into buf, or -1 on I/O error.
         *
         * Used by Zapada.Fs.Vfs.Vfs.Read() to satisfy VFS read requests.
         */
        public static int ReadFile(int startCluster, int totalSize,
                                   byte[] buf, int offset, int count)
        {
            if (s_initialized == 0 || startCluster < 2 || totalSize <= 0)
            {
                return -1;
            }

            int[] secBuf  = new int[128];   /* 512-byte sector buffer */
            int   cluster = startCluster;
            int   written = 0;
            int   filePos = 0;
            int   depth   = 0;

            while (!IsEoc(cluster) && cluster >= 2
                   && depth < MAX_CLUSTER_CHAIN
                   && written < count
                   && filePos < totalSize)
            {
                int lba = ClusterToLba(cluster);
                int sec = 0;

                while (sec < s_spc && written < count && filePos < totalSize)
                {
                    if (Zapada.BlockDev.ReadSector((long)(lba + sec), 1, secBuf) != 0)
                    {
                        return -1;
                    }

                    /* Copy bytes from int[] sector buffer into byte[] buf. */
                    int b = 0;
                    while (b < 512 && written < count && filePos < totalSize)
                    {
                        buf[offset + written] = (byte)BufHelper.GetByte(secBuf, b);
                        written  = written  + 1;
                        filePos  = filePos  + 1;
                        b        = b        + 1;
                    }

                    sec = sec + 1;
                }

                /* Follow the FAT chain to the next cluster. */
                int next = ReadFatEntry(cluster, secBuf);
                if (next < 0)
                {
                    return -1;
                }
                cluster = next;
                depth   = depth + 1;
            }

            return written;
        }
    }
}



