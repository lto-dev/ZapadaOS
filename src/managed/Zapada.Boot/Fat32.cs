/*
 * Zapada - src/managed/Zapada.Boot/Fat32.cs
 *
 * Phase 3A Part 3 — FAT32 filesystem parser.
 *
 * Pure managed C# — no unsafe code, no BCL calls beyond array allocation.
 *
 * FAT32 BPB (BIOS Parameter Block) offsets in the partition boot sector:
 *   11-12 (word):  BytesPerSector    (expect 512)
 *   13    (byte):  SectorsPerCluster
 *   14-15 (word):  ReservedSectors
 *   16    (byte):  FATCount          (number of FATs, usually 2)
 *   22-23 (word):  FATSize16         (0 for FAT32; use FATSize32 instead)
 *   36-39 (dword): FATSize32         (sectors per FAT)
 *   44-47 (dword): RootCluster       (first cluster of root directory)
 *
 * Computed values:
 *   fat_start_lba  = partition_start + ReservedSectors
 *   data_start_lba = fat_start + FATCount * FATSize
 *   ClusterToLba(c) = data_start + (c - 2) * SectorsPerCluster
 *
 * FAT32 chain following:
 *   FAT entry offset in bytes from FAT start: cluster * 4
 *   FAT sector: fat_start + (cluster * 4) / BytesPerSector
 *   Byte in sector: (cluster * 4) % BytesPerSector
 *   Entry value & 0x0FFFFFFF >= 0x0FFFFFF8 => end of chain
 *
 * FAT32 directory entry (32 bytes):
 *   0-7:   Name (8 chars, 0x20-padded, 0x00 = end, 0xE5 = deleted)
 *   8-10:  Extension (3 chars, 0x20-padded)
 *   11:    Attributes (0x0F = LFN entry, skip)
 *   20-21: FirstClusterHigh (word)
 *   26-27: FirstClusterLow  (word)
 *   28-31: FileSize (dword)
 *
 * Test file target: "TEST    DLL" (8.3 uppercase ASCII, space-padded):
 *   Name:  T E S T SPC SPC SPC SPC = 0x54 0x45 0x53 0x54 0x20 0x20 0x20 0x20
 *   Ext:   D L L                   = 0x44 0x4C 0x4C
 *
 * AArch64 alignment: all array accesses are via BufHelper which uses
 * 4-byte-aligned int[] reads + single-byte extraction, safe under -mstrict-align.
 */

using System;

namespace Zapada.Boot
{
    internal static class Fat32
    {
        /* Maximum cluster chain length we follow (prevents infinite loops). */
        private const int MAX_CLUSTER_CHAIN = 64;

        /* Directory entries per 512-byte sector. */
        private const int DIR_ENTRIES_PER_SECTOR = 16;

        /* Directory entry size in bytes. */
        private const int DIR_ENTRY_SIZE = 32;

        /* FAT32 end-of-chain marker (masked with 0x0FFFFFFF). */
        private const int FAT_EOC_MIN = unchecked((int)0x0FFFFFF8);

        /* LFN attribute byte value. */
        private const int ATTR_LFN = 0x0F;

        /* ------------------------------------------------------------------ */
        /* Internal FAT32 volume state (populated by Mount)                    */
        /* ------------------------------------------------------------------ */

        private static int s_bps            = 0;  /* bytes per sector            */
        private static int s_spc            = 0;  /* sectors per cluster         */
        private static int s_fatStart       = 0;  /* FAT1 start LBA              */
        private static int s_dataStart      = 0;  /* data area start LBA         */
        private static int s_rootCluster    = 0;  /* root directory first cluster */

        /* Phase 3B boot scaffolding — replaced by managed VFS in Phase 3.1.
         * Populated by FindTestDll() when TEST.DLL is located.
         * Consumed by ReadTestDll() to walk the cluster chain. */
        private static int s_testDllCluster = 0;
        private static int s_testDllSize    = 0;

        /* Phase 3.1: Populated by FindVblkDll() when VBLK.DLL is located.
         * Consumed by ReadVblkDll() to walk the cluster chain. */
        private static int s_vblkDllCluster = 0;
        private static int s_vblkDllSize    = 0;

        /* ------------------------------------------------------------------ */
        /* Cluster chain helpers                                               */
        /* ------------------------------------------------------------------ */

        private static int ClusterToLba(int cluster)
        {
            return s_dataStart + (cluster - 2) * s_spc;
        }

        /*
         * ReadFatEntry - read the 32-bit FAT entry for <cluster> from the disk.
         *
         * Returns the entry value with the high nibble masked (ECMA FAT32 spec),
         * or -1 on I/O error.
         */
        private static int ReadFatEntry(int cluster, int[] secBuf)
        {
            int byteOff  = cluster * 4;
            int fatSec   = s_fatStart + byteOff / s_bps;
            int inSec    = byteOff % s_bps;

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
             * FAT32 end-of-chain values are 0x0FFFFFF8 through 0x0FFFFFFF.
             * The constant 0x0FFFFFF8 is 268435448 (fits in signed int32).
             */
            return (entry & 0x0FFFFFFF) >= 0x0FFFFFF8;
        }

        /* ------------------------------------------------------------------ */
        /* Name matching helpers                                               */
        /* ------------------------------------------------------------------ */

        /*
         * IsTestDll - returns true when the 11-byte 8.3 name at entOff in buf
         * equals "TEST    DLL" (4 chars, 4 spaces, extension "DLL").
         *
         * Byte values:
         *   [0]='T'=0x54  [1]='E'=0x45  [2]='S'=0x53  [3]='T'=0x54
         *   [4]=0x20  [5]=0x20  [6]=0x20  [7]=0x20
         *   [8]='D'=0x44  [9]='L'=0x4C  [10]='L'=0x4C
         */
        private static bool IsTestDll(int[] buf, int entOff)
        {
            return BufHelper.GetByte(buf, entOff +  0) == 0x54   /* T */
                && BufHelper.GetByte(buf, entOff +  1) == 0x45   /* E */
                && BufHelper.GetByte(buf, entOff +  2) == 0x53   /* S */
                && BufHelper.GetByte(buf, entOff +  3) == 0x54   /* T */
                && BufHelper.GetByte(buf, entOff +  4) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  5) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  6) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  7) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  8) == 0x44   /* D */
                && BufHelper.GetByte(buf, entOff +  9) == 0x4C   /* L */
                && BufHelper.GetByte(buf, entOff + 10) == 0x4C;  /* L */
        }

        /* ------------------------------------------------------------------ */
        /* Mount - parse BPB and initialise static state                       */
        /* ------------------------------------------------------------------ */

        /*
         * Mount(partitionStartLba) - read the FAT32 BPB from the partition
         * boot sector and populate the internal volume state.
         *
         * Returns 0 on success, -1 on I/O error or invalid BPB.
         */
        internal static int Mount(int partitionStartLba)
        {
            int[] bpb = new int[128];   /* 512-byte sector */

            if (Zapada.BlockDev.ReadSector((long)partitionStartLba, 1, bpb) != 0)
            {
                System.Console.Write("[Fat32] I/O error reading BPB\n");
                return -1;
            }

            /* Extract BPB fields. */
            int bps         = BufHelper.GetWord(bpb, 11);  /* BytesPerSector    */
            int spc         = BufHelper.GetByte(bpb, 13);  /* SectorsPerCluster */
            int reserved    = BufHelper.GetWord(bpb, 14);  /* ReservedSectors   */
            int fatCount    = BufHelper.GetByte(bpb, 16);  /* FATCount          */
            int fatSize16   = BufHelper.GetWord(bpb, 22);  /* FATSize16         */
            int fatSize32   = BufHelper.GetDword(bpb, 36); /* FATSize32         */
            int rootCluster = BufHelper.GetDword(bpb, 44); /* RootCluster       */

            /* Validate bytes-per-sector. */
            if (bps != 512)
            {
                System.Console.Write("[Fat32] unsupported sector size\n");
                return -1;
            }

            /* Choose the correct FAT size field. */
            int fatSize = (fatSize16 != 0) ? fatSize16 : fatSize32;
            if (fatSize == 0 || spc == 0)
            {
                System.Console.Write("[Fat32] invalid BPB\n");
                return -1;
            }

            s_bps         = bps;
            s_spc         = spc;
            s_fatStart    = partitionStartLba + reserved;
            s_dataStart   = s_fatStart + fatCount * fatSize;
            s_rootCluster = rootCluster;

            System.Console.Write("[Fat32] mounted: spc=");
            Console.WriteInt(spc);
            System.Console.Write(" root=");
            Console.WriteInt(rootCluster);
            System.Console.Write("\n");

            return 0;
        }

        /* ------------------------------------------------------------------ */
        /* FindTestDll - walk root directory looking for "TEST    DLL"          */
        /* ------------------------------------------------------------------ */

        /*
         * FindTestDll - scan the FAT32 root directory cluster chain for an
         * entry whose 8.3 name equals "TEST    DLL".
         *
         * Prints "[Boot] found: TEST.DLL" and returns 0 when found.
         * Returns -1 when the file is not present.
         *
         * Also prints the first 8 bytes of the file content as hex pairs to
         * satisfy the Gate D requirement: "file contents or first 8 bytes
         * printed to serial".
         */
        internal static int FindTestDll()
        {
            int[]  secBuf  = new int[128];   /* 512-byte sector buffer     */
            int[]  fatBuf  = new int[128];   /* FAT sector buffer (shared) */
            int    cluster = s_rootCluster;
            int    depth   = 0;

            /* Follow the root directory cluster chain. */
            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN)
            {
                /* Read all sectors in this cluster. */
                int clusterLba = ClusterToLba(cluster);
                int sec        = 0;

                while (sec < s_spc)
                {
                    if (Zapada.BlockDev.ReadSector((long)(clusterLba + sec), 1, secBuf) != 0)
                    {
                        System.Console.Write("[Fat32] I/O error reading dir\n");
                        return -1;
                    }

                    /* Scan 16 directory entries in this sector. */
                    int ent = 0;
                    while (ent < DIR_ENTRIES_PER_SECTOR)
                    {
                        int entOff = ent * DIR_ENTRY_SIZE;

                        int first = BufHelper.GetByte(secBuf, entOff + 0);

                        /* 0x00 = end of directory. */
                        if (first == 0x00)
                        {
                            return -1;
                        }

                        /* 0xE5 = deleted entry; 0x0F = LFN; skip both. */
                        if (first != 0xE5)
                        {
                            int attr = BufHelper.GetByte(secBuf, entOff + 11);
                            if (attr != ATTR_LFN)
                            {
                                /* Check for "TEST    DLL". */
                                if (IsTestDll(secBuf, entOff))
                                {
                                    /* Found.  Extract cluster and file size from dir entry. */
                                    int firstClusterHigh = BufHelper.GetWord(secBuf, entOff + 20);
                                    int firstClusterLow  = BufHelper.GetWord(secBuf, entOff + 26);
                                    int fileCluster      = (firstClusterHigh << 16) | firstClusterLow;
                                    int fileSize         = BufHelper.GetDword(secBuf, entOff + 28);

                                    /* Phase 3B boot scaffolding — replaced by managed VFS in Phase 3.1. */
                                    s_testDllCluster = fileCluster;
                                    s_testDllSize    = fileSize;

                                    System.Console.Write("[Boot] found: TEST.DLL\n");

                                    /* Print first 8 bytes of file content as hex. */
                                    PrintFirstBytes(fileCluster, fatBuf, secBuf);
                                    return 0;
                                }
                            }
                        }

                        ent = ent + 1;
                    }

                    sec = sec + 1;
                }

                /* Advance to next cluster in chain. */
                int next = ReadFatEntry(cluster, fatBuf);
                if (next < 0)
                {
                    System.Console.Write("[Fat32] FAT read error\n");
                    return -1;
                }
                cluster = next;
                depth   = depth + 1;
            }

            return -1;
        }

        /* ------------------------------------------------------------------ */
        /* ReadTestDll - Phase 3B boot scaffolding                             */
        /* ------------------------------------------------------------------ */

        /*
         * ReadTestDll() - walk the TEST.DLL cluster chain and return file bytes.
         *
         * Precondition: FindTestDll() must have been called and returned 0;
         * otherwise s_testDllCluster == 0 and this returns null.
         *
         * Returns a byte[] containing the full file, or null on I/O error.
         *
         * Phase 3B boot scaffolding — replaced by managed VFS in Phase 3.1.
         * The Phase 3.1 replacement will be a general ReadFile(string name) method.
         */
        internal static byte[] ReadTestDll()
        {
            if (s_testDllSize <= 0 || s_testDllCluster < 2)
            {
                return null;
            }

            byte[] result  = new byte[s_testDllSize];
            int[]  secBuf  = new int[128];   /* 512-byte sector buffer     */
            int[]  fatBuf  = new int[128];   /* FAT sector buffer          */
            int    cluster = s_testDllCluster;
            int    written = 0;
            int    depth   = 0;

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN
                   && written < s_testDllSize)
            {
                int lba = ClusterToLba(cluster);
                int sec = 0;

                while (sec < s_spc && written < s_testDllSize)
                {
                    if (Zapada.BlockDev.ReadSector((long)(lba + sec), 1, secBuf) != 0)
                    {
                        return null;
                    }

                    /* Copy up to 512 bytes from int[] sector buffer to byte[] result. */
                    int copy = 512;
                    if (written + copy > s_testDllSize)
                    {
                        copy = s_testDllSize - written;
                    }
                    int i = 0;
                    while (i < copy)
                    {
                        result[written] = (byte)BufHelper.GetByte(secBuf, i);
                        written = written + 1;
                        i = i + 1;
                    }

                    sec = sec + 1;
                }

                int next = ReadFatEntry(cluster, fatBuf);
                if (next < 0)
                {
                    return null;
                }
                cluster = next;
                depth   = depth + 1;
            }

            return result;
        }

        /* ------------------------------------------------------------------ */
        /* Phase 3.1: VBLK.DLL support                                        */
        /* ------------------------------------------------------------------ */

        /*
         * IsVblkDll - returns true when the 11-byte 8.3 name at entOff in buf
         * equals "VBLK    DLL" (4 chars, 4 spaces, extension "DLL").
         *
         * Byte values:
         *   [0]='V'=0x56  [1]='B'=0x42  [2]='L'=0x4C  [3]='K'=0x4B
         *   [4]=0x20  [5]=0x20  [6]=0x20  [7]=0x20
         *   [8]='D'=0x44  [9]='L'=0x4C  [10]='L'=0x4C
         */
        private static bool IsVblkDll(int[] buf, int entOff)
        {
            return BufHelper.GetByte(buf, entOff +  0) == 0x56   /* V */
                && BufHelper.GetByte(buf, entOff +  1) == 0x42   /* B */
                && BufHelper.GetByte(buf, entOff +  2) == 0x4C   /* L */
                && BufHelper.GetByte(buf, entOff +  3) == 0x4B   /* K */
                && BufHelper.GetByte(buf, entOff +  4) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  5) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  6) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  7) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  8) == 0x44   /* D */
                && BufHelper.GetByte(buf, entOff +  9) == 0x4C   /* L */
                && BufHelper.GetByte(buf, entOff + 10) == 0x4C;  /* L */
        }

        /*
         * FindVblkDll - scan the FAT32 root directory cluster chain for an
         * entry whose 8.3 name equals "VBLK    DLL".
         *
         * Populates s_vblkDllCluster and s_vblkDllSize on success.
         * Returns 0 when found, -1 when not found or on I/O error.
         *
         * Phase 3.1: permanent runtime support for VirtioBlock driver DLL.
         */
        internal static int FindVblkDll()
        {
            int[]  secBuf  = new int[128];
            int[]  fatBuf  = new int[128];
            int    cluster = s_rootCluster;
            int    depth   = 0;

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN)
            {
                int clusterLba = ClusterToLba(cluster);
                int sec        = 0;

                while (sec < s_spc)
                {
                    if (Zapada.BlockDev.ReadSector((long)(clusterLba + sec), 1, secBuf) != 0)
                    {
                        System.Console.Write("[Fat32] I/O error reading dir\n");
                        return -1;
                    }

                    int ent = 0;
                    while (ent < DIR_ENTRIES_PER_SECTOR)
                    {
                        int entOff = ent * DIR_ENTRY_SIZE;
                        int first  = BufHelper.GetByte(secBuf, entOff + 0);

                        if (first == 0x00) { return -1; }

                        if (first != 0xE5)
                        {
                            int attr = BufHelper.GetByte(secBuf, entOff + 11);
                            if (attr != ATTR_LFN)
                            {
                                if (IsVblkDll(secBuf, entOff))
                                {
                                    int firstClusterHigh = BufHelper.GetWord(secBuf, entOff + 20);
                                    int firstClusterLow  = BufHelper.GetWord(secBuf, entOff + 26);
                                    s_vblkDllCluster     = (firstClusterHigh << 16) | firstClusterLow;
                                    s_vblkDllSize        = BufHelper.GetDword(secBuf, entOff + 28);
                                    return 0;
                                }
                            }
                        }

                        ent = ent + 1;
                    }

                    sec = sec + 1;
                }

                int next = ReadFatEntry(cluster, fatBuf);
                if (next < 0)
                {
                    System.Console.Write("[Fat32] FAT read error\n");
                    return -1;
                }
                cluster = next;
                depth   = depth + 1;
            }

            return -1;
        }

        /*
         * ReadVblkDll() - walk the VBLK.DLL cluster chain and return file bytes.
         *
         * Precondition: FindVblkDll() must have been called and returned 0.
         * Returns a byte[] containing the full file, or null on I/O error.
         *
         * Phase 3.1: permanent runtime support for VirtioBlock driver DLL.
         */
        internal static byte[] ReadVblkDll()
        {
            if (s_vblkDllSize <= 0 || s_vblkDllCluster < 2)
            {
                return null;
            }

            byte[] result  = new byte[s_vblkDllSize];
            int[]  secBuf  = new int[128];
            int[]  fatBuf  = new int[128];
            int    cluster = s_vblkDllCluster;
            int    written = 0;
            int    depth   = 0;

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN
                   && written < s_vblkDllSize)
            {
                int lba = ClusterToLba(cluster);
                int sec = 0;

                while (sec < s_spc && written < s_vblkDllSize)
                {
                    if (Zapada.BlockDev.ReadSector((long)(lba + sec), 1, secBuf) != 0)
                    {
                        return null;
                    }

                    int copy = 512;
                    if (written + copy > s_vblkDllSize)
                    {
                        copy = s_vblkDllSize - written;
                    }

                    int i = 0;
                    while (i < copy)
                    {
                        result[written] = (byte)BufHelper.GetByte(secBuf, i);
                        written         = written + 1;
                        i               = i + 1;
                    }

                    sec = sec + 1;
                }

                int next = ReadFatEntry(cluster, fatBuf);
                if (next < 0)
                {
                    return null;
                }
                cluster = next;
                depth   = depth + 1;
            }

            return result;
        }

        /* ------------------------------------------------------------------ */
        /* PrintFirstBytes - read first cluster of a file, print 8 bytes hex  */
        /* ------------------------------------------------------------------ */

        private static void PrintFirstBytes(int fileCluster, int[] fatBuf, int[] secBuf)
        {
            /* Unused parameter — reuse secBuf as file read buffer. */
            if (fileCluster < 2)
            {
                System.Console.Write("[Boot] file empty\n");
                return;
            }

            int lba = ClusterToLba(fileCluster);
            if (Zapada.BlockDev.ReadSector((long)lba, 1, secBuf) != 0)
            {
                System.Console.Write("[Boot] file read error\n");
                return;
            }

            /* Print first 8 bytes as "XX XX XX XX XX XX XX XX". */
            System.Console.Write("[Boot] bytes: ");
            int i = 0;
            while (i < 8)
            {
                if (i != 0)
                {
                    System.Console.Write(" ");
                }
                Console.WriteHex(BufHelper.GetByte(secBuf, i));
                i = i + 1;
            }
            System.Console.Write("\n");
        }

        /* ------------------------------------------------------------------ */
        /* Phase 3.1 D.2: GPT.DLL support                                     */
        /* ------------------------------------------------------------------ */

        /* Phase 3.1 D.2: populated by FindGptDll(), consumed by ReadGptDll(). */
        private static int s_gptDllCluster = 0;
        private static int s_gptDllSize    = 0;

        /*
         * IsGptDll - returns true when the 11-byte 8.3 name at entOff in buf
         * equals "GPT     DLL" (3 chars, 5 spaces, extension "DLL").
         *
         * Byte values:
         *   [0]='G'=0x47  [1]='P'=0x50  [2]='T'=0x54
         *   [3]=0x20  [4]=0x20  [5]=0x20  [6]=0x20  [7]=0x20
         *   [8]='D'=0x44  [9]='L'=0x4C  [10]='L'=0x4C
         */
        private static bool IsGptDll(int[] buf, int entOff)
        {
            return BufHelper.GetByte(buf, entOff +  0) == 0x47   /* G */
                && BufHelper.GetByte(buf, entOff +  1) == 0x50   /* P */
                && BufHelper.GetByte(buf, entOff +  2) == 0x54   /* T */
                && BufHelper.GetByte(buf, entOff +  3) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  4) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  5) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  6) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  7) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  8) == 0x44   /* D */
                && BufHelper.GetByte(buf, entOff +  9) == 0x4C   /* L */
                && BufHelper.GetByte(buf, entOff + 10) == 0x4C;  /* L */
        }

        /*
         * FindGptDll - scan the FAT32 root directory cluster chain for an
         * entry whose 8.3 name equals "GPT     DLL".
         *
         * Populates s_gptDllCluster and s_gptDllSize on success.
         * Returns 0 when found, -1 when not found or on I/O error.
         */
        internal static int FindGptDll()
        {
            int[]  secBuf  = new int[128];
            int[]  fatBuf  = new int[128];
            int    cluster = s_rootCluster;
            int    depth   = 0;

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN)
            {
                int clusterLba = ClusterToLba(cluster);
                int sec        = 0;

                while (sec < s_spc)
                {
                    if (Zapada.BlockDev.ReadSector((long)(clusterLba + sec), 1, secBuf) != 0)
                    {
                        System.Console.Write("[Fat32] I/O error reading dir\n");
                        return -1;
                    }

                    int ent = 0;
                    while (ent < DIR_ENTRIES_PER_SECTOR)
                    {
                        int entOff = ent * DIR_ENTRY_SIZE;
                        int first  = BufHelper.GetByte(secBuf, entOff + 0);

                        if (first == 0x00) { return -1; }

                        if (first != 0xE5)
                        {
                            int attr = BufHelper.GetByte(secBuf, entOff + 11);
                            if (attr != ATTR_LFN)
                            {
                                if (IsGptDll(secBuf, entOff))
                                {
                                    int firstClusterHigh = BufHelper.GetWord(secBuf, entOff + 20);
                                    int firstClusterLow  = BufHelper.GetWord(secBuf, entOff + 26);
                                    s_gptDllCluster      = (firstClusterHigh << 16) | firstClusterLow;
                                    s_gptDllSize         = BufHelper.GetDword(secBuf, entOff + 28);
                                    return 0;
                                }
                            }
                        }

                        ent = ent + 1;
                    }

                    sec = sec + 1;
                }

                int next = ReadFatEntry(cluster, fatBuf);
                if (next < 0)
                {
                    System.Console.Write("[Fat32] FAT read error\n");
                    return -1;
                }
                cluster = next;
                depth   = depth + 1;
            }

            return -1;
        }

        /*
         * ReadGptDll() - walk the GPT.DLL cluster chain and return file bytes.
         *
         * Precondition: FindGptDll() must have been called and returned 0.
         * Returns a byte[] containing the full file, or null on I/O error.
         */
        internal static byte[] ReadGptDll()
        {
            if (s_gptDllSize <= 0 || s_gptDllCluster < 2)
            {
                return null;
            }

            byte[] result  = new byte[s_gptDllSize];
            int[]  secBuf  = new int[128];
            int[]  fatBuf  = new int[128];
            int    cluster = s_gptDllCluster;
            int    written = 0;
            int    depth   = 0;

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN
                   && written < s_gptDllSize)
            {
                int lba = ClusterToLba(cluster);
                int sec = 0;

                while (sec < s_spc && written < s_gptDllSize)
                {
                    if (Zapada.BlockDev.ReadSector((long)(lba + sec), 1, secBuf) != 0)
                    {
                        return null;
                    }

                    int copy = 512;
                    if (written + copy > s_gptDllSize)
                    {
                        copy = s_gptDllSize - written;
                    }

                    int i = 0;
                    while (i < copy)
                    {
                        result[written] = (byte)BufHelper.GetByte(secBuf, i);
                        written         = written + 1;
                        i               = i + 1;
                    }

                    sec = sec + 1;
                }

                int next = ReadFatEntry(cluster, fatBuf);
                if (next < 0)
                {
                    return null;
                }
                cluster = next;
                depth   = depth + 1;
            }

            return result;
        }

        /* ------------------------------------------------------------------ */
        /* Phase 3.1 D.3: FAT32.DLL support                                   */
        /* ------------------------------------------------------------------ */

        /* Phase 3.1 D.3: populated by FindFat32Dll(), consumed by ReadFat32Dll(). */
        private static int s_fat32DllCluster = 0;
        private static int s_fat32DllSize    = 0;

        /*
         * IsFat32Dll - returns true when the 11-byte 8.3 name at entOff in buf
         * equals "FAT32   DLL" (5 chars, 3 spaces, extension "DLL").
         *
         * Byte values:
         *   [0]='F'=0x46  [1]='A'=0x41  [2]='T'=0x54
         *   [3]='3'=0x33  [4]='2'=0x32
         *   [5]=0x20  [6]=0x20  [7]=0x20
         *   [8]='D'=0x44  [9]='L'=0x4C  [10]='L'=0x4C
         */
        private static bool IsFat32Dll(int[] buf, int entOff)
        {
            return BufHelper.GetByte(buf, entOff +  0) == 0x46   /* F */
                && BufHelper.GetByte(buf, entOff +  1) == 0x41   /* A */
                && BufHelper.GetByte(buf, entOff +  2) == 0x54   /* T */
                && BufHelper.GetByte(buf, entOff +  3) == 0x33   /* 3 */
                && BufHelper.GetByte(buf, entOff +  4) == 0x32   /* 2 */
                && BufHelper.GetByte(buf, entOff +  5) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  6) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  7) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  8) == 0x44   /* D */
                && BufHelper.GetByte(buf, entOff +  9) == 0x4C   /* L */
                && BufHelper.GetByte(buf, entOff + 10) == 0x4C;  /* L */
        }

        /*
         * FindFat32Dll - scan the FAT32 root directory cluster chain for an
         * entry whose 8.3 name equals "FAT32   DLL".
         *
         * Populates s_fat32DllCluster and s_fat32DllSize on success.
         * Returns 0 when found, -1 when not found or on I/O error.
         */
        internal static int FindFat32Dll()
        {
            int[]  secBuf  = new int[128];
            int[]  fatBuf  = new int[128];
            int    cluster = s_rootCluster;
            int    depth   = 0;

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN)
            {
                int clusterLba = ClusterToLba(cluster);
                int sec        = 0;

                while (sec < s_spc)
                {
                    if (Zapada.BlockDev.ReadSector((long)(clusterLba + sec), 1, secBuf) != 0)
                    {
                        System.Console.Write("[Fat32] I/O error reading dir\n");
                        return -1;
                    }

                    int ent = 0;
                    while (ent < DIR_ENTRIES_PER_SECTOR)
                    {
                        int entOff = ent * DIR_ENTRY_SIZE;
                        int first  = BufHelper.GetByte(secBuf, entOff + 0);

                        if (first == 0x00) { return -1; }

                        if (first != 0xE5)
                        {
                            int attr = BufHelper.GetByte(secBuf, entOff + 11);
                            if (attr != ATTR_LFN)
                            {
                                if (IsFat32Dll(secBuf, entOff))
                                {
                                    int firstClusterHigh = BufHelper.GetWord(secBuf, entOff + 20);
                                    int firstClusterLow  = BufHelper.GetWord(secBuf, entOff + 26);
                                    s_fat32DllCluster    = (firstClusterHigh << 16) | firstClusterLow;
                                    s_fat32DllSize       = BufHelper.GetDword(secBuf, entOff + 28);
                                    return 0;
                                }
                            }
                        }

                        ent = ent + 1;
                    }

                    sec = sec + 1;
                }

                int next = ReadFatEntry(cluster, fatBuf);
                if (next < 0) { return -1; }
                cluster = next;
                depth   = depth + 1;
            }

            return -1;
        }

        /*
         * ReadFat32Dll() - walk the FAT32.DLL cluster chain and return file bytes.
         *
         * Precondition: FindFat32Dll() must have been called and returned 0.
         * Returns a byte[] containing the full file, or null on I/O error.
         */
        internal static byte[] ReadFat32Dll()
        {
            if (s_fat32DllSize <= 0 || s_fat32DllCluster < 2)
            {
                return null;
            }

            byte[] result  = new byte[s_fat32DllSize];
            int[]  secBuf  = new int[128];
            int[]  fatBuf  = new int[128];
            int    cluster = s_fat32DllCluster;
            int    written = 0;
            int    depth   = 0;

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN
                   && written < s_fat32DllSize)
            {
                int clusterLba = ClusterToLba(cluster);
                int sec        = 0;

                while (sec < s_spc && written < s_fat32DllSize)
                {
                    if (Zapada.BlockDev.ReadSector((long)(clusterLba + sec), 1, secBuf) != 0)
                    {
                        return null;
                    }

                    int copy = 512;
                    if (written + copy > s_fat32DllSize)
                    {
                        copy = s_fat32DllSize - written;
                    }

                    int i = 0;
                    while (i < copy)
                    {
                        result[written] = (byte)BufHelper.GetByte(secBuf, i);
                        written         = written + 1;
                        i               = i + 1;
                    }

                    sec = sec + 1;
                }

                int next = ReadFatEntry(cluster, fatBuf);
                if (next < 0)
                {
                    return null;
                }
                cluster = next;
                depth   = depth + 1;
            }

            return result;
        }

        /* ------------------------------------------------------------------ */
        /* Phase 3.1 D.4: VFS.DLL support                                     */
        /* ------------------------------------------------------------------ */

        /* Phase 3.1 D.4: populated by FindVfsDll(), consumed by ReadVfsDll(). */
        private static int s_vfsDllCluster = 0;
        private static int s_vfsDllSize    = 0;

        /*
         * IsVfsDll - returns true when the 11-byte 8.3 name at entOff in buf
         * equals "VFS     DLL" (3 chars, 5 spaces, extension "DLL").
         *
         * Byte values:
         *   [0]='V'=0x56  [1]='F'=0x46  [2]='S'=0x53
         *   [3]=0x20  [4]=0x20  [5]=0x20  [6]=0x20  [7]=0x20
         *   [8]='D'=0x44  [9]='L'=0x4C  [10]='L'=0x4C
         */
        private static bool IsVfsDll(int[] buf, int entOff)
        {
            return BufHelper.GetByte(buf, entOff +  0) == 0x56   /* V */
                && BufHelper.GetByte(buf, entOff +  1) == 0x46   /* F */
                && BufHelper.GetByte(buf, entOff +  2) == 0x53   /* S */
                && BufHelper.GetByte(buf, entOff +  3) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  4) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  5) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  6) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  7) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  8) == 0x44   /* D */
                && BufHelper.GetByte(buf, entOff +  9) == 0x4C   /* L */
                && BufHelper.GetByte(buf, entOff + 10) == 0x4C;  /* L */
        }

        /*
         * FindVfsDll - scan the FAT32 root directory cluster chain for an
         * entry whose 8.3 name equals "VFS     DLL".
         *
         * Populates s_vfsDllCluster and s_vfsDllSize on success.
         * Returns 0 when found, -1 when not found or on I/O error.
         */
        internal static int FindVfsDll()
        {
            int[]  secBuf  = new int[128];
            int    cluster = s_rootCluster;
            int    depth   = 0;

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN)
            {
                int lba = ClusterToLba(cluster);
                int sec = 0;

                while (sec < s_spc)
                {
                    if (Zapada.BlockDev.ReadSector((long)(lba + sec), 1, secBuf) != 0)
                    {
                        return -1;
                    }

                    int off = 0;
                    while (off < 512)
                    {
                        int first = BufHelper.GetByte(secBuf, off);
                        if (first == 0x00) { return -1; }
                        if (first == 0xE5) { off = off + 32; continue; }

                        int attr = BufHelper.GetByte(secBuf, off + 11);
                        if (attr == 0x0F)  { off = off + 32; continue; }

                        if (IsVfsDll(secBuf, off))
                        {
                            int hi   = BufHelper.GetByte(secBuf, off + 20)
                                     | (BufHelper.GetByte(secBuf, off + 21) << 8);
                            int lo   = BufHelper.GetByte(secBuf, off + 26)
                                     | (BufHelper.GetByte(secBuf, off + 27) << 8);
                            int sz   = BufHelper.GetByte(secBuf, off + 28)
                                     | (BufHelper.GetByte(secBuf, off + 29) << 8)
                                     | (BufHelper.GetByte(secBuf, off + 30) << 16)
                                     | (BufHelper.GetByte(secBuf, off + 31) << 24);

                            s_vfsDllCluster = (hi << 16) | lo;
                            s_vfsDllSize    = sz;
                            return 0;
                        }

                        off = off + 32;
                    }

                    sec = sec + 1;
                }

                int nextC = ReadFatEntry(cluster, secBuf);
                if (nextC < 0) { return -1; }
                cluster = nextC;
                depth   = depth + 1;
            }

            return -1;
        }

        /*
         * ReadVfsDll - walk the cluster chain starting at s_vfsDllCluster and
         * copy s_vfsDllSize bytes into a freshly-allocated byte[].
         *
         * Returns null on I/O error or if FindVfsDll() was not called first.
         */
        internal static byte[]? ReadVfsDll()
        {
            if (s_vfsDllCluster < 2 || s_vfsDllSize <= 0)
                return null;

            byte[] result  = new byte[s_vfsDllSize];
            int[]  secBuf  = new int[128];
            int[]  fatBuf  = new int[128];
            int    cluster = s_vfsDllCluster;
            int    written = 0;
            int    depth   = 0;

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN
                   && written < s_vfsDllSize)
            {
                int lba = ClusterToLba(cluster);
                int sec = 0;

                while (sec < s_spc && written < s_vfsDllSize)
                {
                    if (Zapada.BlockDev.ReadSector((long)(lba + sec), 1, secBuf) != 0)
                    {
                        return null;
                    }

                    int copy = 512;
                    if (written + copy > s_vfsDllSize)
                    {
                        copy = s_vfsDllSize - written;
                    }

                    int i = 0;
                    while (i < copy)
                    {
                        result[written] = (byte)BufHelper.GetByte(secBuf, i);
                        written         = written + 1;
                        i               = i + 1;
                    }

                    sec = sec + 1;
                }

                int next = ReadFatEntry(cluster, fatBuf);
                if (next < 0)
                {
                    return null;
                }
                cluster = next;
                depth   = depth + 1;
            }

            return result;
        }

        /* Phase 3.2 Conf: populated by FindConfDll(), consumed by ReadConfDll(). */
        private static int s_confDllCluster = 0;
        private static int s_confDllSize    = 0;

        /*
         * IsConfDll - returns true when the 11-byte 8.3 name at entOff in buf
         * equals "CONF    DLL" (4 chars, 4 spaces, extension "DLL").
         *
         * Byte values:
         *   [0]='C'=0x43  [1]='O'=0x4F  [2]='N'=0x4E  [3]='F'=0x46
         *   [4]=0x20  [5]=0x20  [6]=0x20  [7]=0x20
         *   [8]='D'=0x44  [9]='L'=0x4C  [10]='L'=0x4C
         */
        private static bool IsConfDll(int[] buf, int entOff)
        {
            return BufHelper.GetByte(buf, entOff +  0) == 0x43   /* C */
                && BufHelper.GetByte(buf, entOff +  1) == 0x4F   /* O */
                && BufHelper.GetByte(buf, entOff +  2) == 0x4E   /* N */
                && BufHelper.GetByte(buf, entOff +  3) == 0x46   /* F */
                && BufHelper.GetByte(buf, entOff +  4) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  5) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  6) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  7) == 0x20   /* SPC */
                && BufHelper.GetByte(buf, entOff +  8) == 0x44   /* D */
                && BufHelper.GetByte(buf, entOff +  9) == 0x4C   /* L */
                && BufHelper.GetByte(buf, entOff + 10) == 0x4C;  /* L */
        }

        /*
         * FindConfDll - scan the FAT32 root directory cluster chain for an
         * entry whose 8.3 name equals "CONF    DLL".
         *
         * Populates s_confDllCluster and s_confDllSize on success.
         * Returns 0 when found, -1 when not found or on I/O error.
         */
        internal static int FindConfDll()
        {
            int[]  secBuf  = new int[128];
            int    cluster = s_rootCluster;
            int    depth   = 0;

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN)
            {
                int lba = ClusterToLba(cluster);
                int sec = 0;

                while (sec < s_spc)
                {
                    if (Zapada.BlockDev.ReadSector((long)(lba + sec), 1, secBuf) != 0)
                    {
                        return -1;
                    }

                    int off = 0;
                    while (off < 512)
                    {
                        int first = BufHelper.GetByte(secBuf, off);
                        if (first == 0x00) { return -1; }
                        if (first == 0xE5) { off = off + 32; continue; }

                        int attr = BufHelper.GetByte(secBuf, off + 11);
                        if (attr == 0x0F)  { off = off + 32; continue; }

                        if (IsConfDll(secBuf, off))
                        {
                            int hi   = BufHelper.GetByte(secBuf, off + 20)
                                     | (BufHelper.GetByte(secBuf, off + 21) << 8);
                            int lo   = BufHelper.GetByte(secBuf, off + 26)
                                     | (BufHelper.GetByte(secBuf, off + 27) << 8);
                            int sz   = BufHelper.GetByte(secBuf, off + 28)
                                     | (BufHelper.GetByte(secBuf, off + 29) << 8)
                                     | (BufHelper.GetByte(secBuf, off + 30) << 16)
                                     | (BufHelper.GetByte(secBuf, off + 31) << 24);

                            s_confDllCluster = (hi << 16) | lo;
                            s_confDllSize    = sz;
                            return 0;
                        }

                        off = off + 32;
                    }

                    sec = sec + 1;
                }

                int nextC = ReadFatEntry(cluster, secBuf);
                if (nextC < 0) { return -1; }
                cluster = nextC;
                depth   = depth + 1;
            }

            return -1;
        }

        /*
         * ReadConfDll - walk the cluster chain starting at s_confDllCluster and
         * copy s_confDllSize bytes into a freshly-allocated byte[].
         *
         * Returns null on I/O error or if FindConfDll() was not called first.
         */
        internal static byte[]? ReadConfDll()
        {
            if (s_confDllCluster < 2 || s_confDllSize <= 0)
                return null;

            byte[] result  = new byte[s_confDllSize];
            int[]  secBuf  = new int[128];
            int[]  fatBuf  = new int[128];
            int    cluster = s_confDllCluster;
            int    written = 0;
            int    depth   = 0;

            while (!IsEoc(cluster) && cluster >= 2 && depth < MAX_CLUSTER_CHAIN
                   && written < s_confDllSize)
            {
                int lba = ClusterToLba(cluster);
                int sec = 0;

                while (sec < s_spc && written < s_confDllSize)
                {
                    if (Zapada.BlockDev.ReadSector((long)(lba + sec), 1, secBuf) != 0)
                    {
                        return null;
                    }

                    int copy = 512;
                    if (written + copy > s_confDllSize)
                    {
                        copy = s_confDllSize - written;
                    }

                    int i = 0;
                    while (i < copy)
                    {
                        result[written] = (byte)BufHelper.GetByte(secBuf, i);
                        written         = written + 1;
                        i               = i + 1;
                    }

                    sec = sec + 1;
                }

                int next = ReadFatEntry(cluster, fatBuf);
                if (next < 0)
                {
                    return null;
                }
                cluster = next;
                depth   = depth + 1;
            }

            return result;
        }
    }
}


