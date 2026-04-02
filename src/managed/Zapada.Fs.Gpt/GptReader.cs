/*
 * Zapada - src/managed/Zapada.Fs.Gpt/GptReader.cs
 *
 * Phase 3.1 D.2: Managed GPT partition table reader.
 *
 * GptReader is the data-access layer for the Zapada.Fs.Gpt driver.
 * It reads the protective MBR, GPT header, and partition entries, and
 * validates header and partition-entry-array CRCs using Crc32.
 *
 * Phase 3.1 D.2 scope: type registration and self-identification.
 * Full GPT read/validate is activated in Phase 3.1 D.3+ when the FAT32
 * managed driver and VFS are in place to service block reads from managed code.
 *
 * CIL constraint: no generics, no delegates, no foreach/yield, no LINQ.
 * All loops use explicit while/for with integer counters.
 */

namespace Zapada.Fs
{
    /*
     * GptPartitionEntry - minimal struct-like class representing one GPT entry.
     * Used by GptReader.GetPartitionEntry() in Phase 3.1 D.3+.
     */
    internal sealed class GptPartitionEntry
    {
        internal long StartLba;
        internal long EndLba;
        internal int  Flags;

        internal GptPartitionEntry()
        {
            StartLba = 0;
            EndLba   = 0;
            Flags    = 0;
        }
    }

    /*
     * GptReader - reads the GPT header at LBA 1 and the partition entry array.
     *
     * Requires:
     *   Zapada.BlockDev.ReadSector() InternalCall to be registered before use.
     *
     * Phase 3.1 D.2: class declared; Initialize() called by DllMain only to
     * confirm the type is present in the loaded assembly.
     * Phase 3.1 D.3+: GetPartitionCount() and GetPartitionEntry() are wired
     * to the native block read path.
     */
    internal static class GptReader
    {
        private static int s_partitionCount = 0;

        /*
         * Initialize() - called once by DllMain.Initialize() during driver load.
         * In Phase 3.1 D.2 this records that the reader type is present.
         * Full GPT header validation and CRC32 check are deferred to D.3+.
         */
        internal static void Initialize()
        {
            s_partitionCount = 0;
        }

        internal static int GetPartitionCount()
        {
            return s_partitionCount;
        }
    }
}


