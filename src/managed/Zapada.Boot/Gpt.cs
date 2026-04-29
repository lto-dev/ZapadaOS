/*
 * Zapada - src/managed/Zapada.Boot/Gpt.cs
 *
 * Phase 3A Part 3 — GPT (GUID Partition Table) parser.
 *
 * Pure managed C# — no unsafe code, no BCL calls beyond array allocation.
 *
 * GPT disk layout (UEFI specification, §5.3):
 *   LBA 0:  Protective MBR (first 8 bytes = ZAPADASK signature on Zapada disks)
 *   LBA 1:  GPT header
 *   LBA 2+: Partition entry array (128 bytes per entry, typically 32 sectors)
 *   LBA N-33..N-2: Secondary partition array
 *   LBA N-1: Backup GPT header
 *
 * GPT header fields used (offsets in bytes):
 *   0-7:   Signature "EFI PART" (little-endian: 0x20494645, 0x54524150)
 *   72-79: PartitionEntryLBA (int64)
 *   80-83: NumberOfPartitionEntries (int32, standard 128)
 *   84-87: SizeOfPartitionEntry (int32, standard 128)
 *
 * GPT partition entry fields used (offsets from entry start):
 *   0-15:  PartitionTypeGUID (all zeros = empty entry, skip)
 *   32-39: StartingLBA (int64)
 *   40-47: EndingLBA (int64)
 *   56-127: PartitionName (UTF-16LE, max 36 chars = 72 bytes)
 *
 * Name comparison strategy:
 *   "ZAPADA_BOOT" (10 UTF-16LE chars) is compared as 5 little-endian int32
 *   values, then verified by a null terminator check at byte 20 of the name.
 *   This avoids string indexing (which would require String.get_Chars BCL calls
 *   the interpreter does not support).
 *
 *   "ZAPADA_BOOT" as UTF-16LE int32 pairs:
 *     entOff+56: {C=0x43,0x00,Y=0x59,0x00} -> GetDword == 0x00590043
 *     entOff+60: {L=0x4C,0x00,I=0x49,0x00} -> GetDword == 0x0049004C
 *     entOff+64: {X=0x58,0x00,_=0x5F,0x00} -> GetDword == 0x005F0058
 *     entOff+68: {B=0x42,0x00,O=0x4F,0x00} -> GetDword == 0x004F0042
 *     entOff+72: {O=0x4F,0x00,T=0x54,0x00} -> GetDword == 0x0054004F
 *     entOff+76: {0x00,0x00,...}            -> null terminator
 */

using System;

namespace Zapada.Boot
{
    internal sealed class GptPartitionInfo
    {
        internal long StartLba;
        internal long EndLba;
        internal long SectorCount;
    }

    internal static class Gpt
    {
        /* GPT header magic words (little-endian int32 view of "EFI PART") */
        private const int GPT_SIG_LO = 0x20494645;  /* "EFI " */
        private const int GPT_SIG_HI = 0x54524150;  /* "PART" */

        /* Standard GPT partition entry size */
        private const int GPT_ENTRY_SIZE = 128;

        /* Number of 128-byte entries that fit in one 512-byte sector */
        private const int GPT_ENTRIES_PER_SECTOR = 4;

        /* ------------------------------------------------------------------
         * IsEntryEmpty - returns true when the type GUID (bytes 0-15) is
         * all zero, which marks an unused partition entry.
         * ------------------------------------------------------------------ */
        private static bool IsEntryEmpty(int[] buf, int entOff)
        {
            return BufHelper.GetDword(buf, entOff + 0)  == 0
                && BufHelper.GetDword(buf, entOff + 4)  == 0
                && BufHelper.GetDword(buf, entOff + 8)  == 0
                && BufHelper.GetDword(buf, entOff + 12) == 0;
        }

        private static bool NameMatches(int[] buf, int entOff, string name)
        {
            int nameOff = entOff + 56;
            int i = 0;
            while (i < name.Length)
            {
                if (BufHelper.GetByte(buf, nameOff + i * 2) != name[i])
                    return false;

                if (BufHelper.GetByte(buf, nameOff + i * 2 + 1) != 0)
                    return false;

                i = i + 1;
            }

            return BufHelper.GetByte(buf, nameOff + i * 2) == 0
                && BufHelper.GetByte(buf, nameOff + i * 2 + 1) == 0;
        }

        /* ------------------------------------------------------------------
         * FindPartitionByName
         *
         * Reads the GPT header from LBA 1, then iterates the partition entry
         * array looking for a partition with the requested name.
         *
         * Returns the StartingLBA of the partition (fits in int32 for our test
         * disk), or -1 if not found or on I/O error.
         * ------------------------------------------------------------------ */
        internal static int FindPartitionByName(string partitionName)
        {
            GptPartitionInfo info = FindPartitionInfoByName(partitionName);
            if (info == null)
                return -1;

            return (int)info.StartLba;
        }

        internal static GptPartitionInfo FindPartitionInfoByName(string partitionName)
        {
            int[] hdrBuf = new int[128];   /* 512 bytes / 4 = 128 int32 elements */
            int[] entBuf = new int[128];

            if (partitionName == null || partitionName.Length == 0)
                return null;

            /* Read GPT header at LBA 1. */
            if (Zapada.BlockDev.ReadSector(1L, 1, hdrBuf) != 0)
            {
                Console.Write("[Gpt] I/O error reading LBA 1\n");
                return null;
            }

            /* Verify GPT signature. */
            if (BufHelper.GetDword(hdrBuf, 0) != GPT_SIG_LO ||
                BufHelper.GetDword(hdrBuf, 4) != GPT_SIG_HI)
            {
                Console.Write("[Gpt] bad signature\n");
                return null;
            }

            /* Read partition table parameters from the header. */
            long ptLba      = BufHelper.GetQword(hdrBuf, 72);  /* PartitionEntryLBA    */
            int  entCount   = BufHelper.GetDword(hdrBuf, 80);  /* NumPartitionEntries  */
            int  entSize    = BufHelper.GetDword(hdrBuf, 84);  /* SizeOfPartitionEntry */

            Console.Write("[Gpt] header ptLba=");
            Console.Write((int)ptLba);
            Console.Write(" entCount=");
            Console.Write(entCount);
            Console.Write(" entSize=");
            Console.Write(entSize);
            Console.Write("\n");

            /* We only support the standard 128-byte entry size. */
            if (entSize != GPT_ENTRY_SIZE || entCount <= 0)
            {
                Console.Write("[Gpt] unsupported entry format\n");
                return null;
            }

            /* Scan partition entries: 4 per sector (128 bytes each). */
            int secIdx  = 0;
            int entIdx  = 0;

            while (entIdx < entCount)
            {
                /* Load a new partition table sector when we start a new group of 4. */
                int entInSec = entIdx % GPT_ENTRIES_PER_SECTOR;
                if (entInSec == 0)
                {
                    long secLba = ptLba + (long)secIdx;
                    if (Zapada.BlockDev.ReadSector(secLba, 1, entBuf) != 0)
                    {
                        Console.Write("[Gpt] I/O error reading partition table\n");
                        return null;
                    }
                    secIdx = secIdx + 1;
                }

                int entOff = entInSec * GPT_ENTRY_SIZE;

                /* Skip empty (unused) entries. */
                if (!IsEntryEmpty(entBuf, entOff))
                {
                    long startLba = BufHelper.GetQword(entBuf, entOff + 32);
                    long endLba   = BufHelper.GetQword(entBuf, entOff + 40);

                    Console.Write("[Gpt] entry idx=");
                    Console.Write(entIdx);
                    Console.Write(" start=");
                    Console.Write((int)startLba);
                    Console.Write(" end=");
                    Console.Write((int)endLba);
                    Console.Write("\n");

                    if (NameMatches(entBuf, entOff, partitionName))
                    {
                        /* Return starting LBA (truncated to int32, safe for 100 MiB disk). */
                        Console.Write("[Gpt] matched ");
                        Console.Write(partitionName);
                        Console.Write(" idx=");
                        Console.Write(entIdx);
                        Console.Write("\n");
                        GptPartitionInfo info = new GptPartitionInfo();
                        info.StartLba = startLba;
                        info.EndLba = endLba;
                        info.SectorCount = endLba - startLba + 1;
                        return info;
                    }
                }

                entIdx = entIdx + 1;
            }

            Console.Write("[Gpt] partition not found: ");
            Console.Write(partitionName);
            Console.Write("\n");
            return null;
        }

        internal static int FindZapadaBootPartition()
        {
            return FindPartitionByName("ZAPADA_BOOT");
        }

        internal static int FindZapadaRootPartition()
        {
            return FindPartitionByName("ZAPADA_BOOT");
        }

    }
}



