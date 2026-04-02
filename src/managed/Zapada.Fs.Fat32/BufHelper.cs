/*
 * Zapada - src/managed/Zapada.Fs.Fat32/BufHelper.cs
 *
 * Pure-managed byte buffer extraction helpers for the FAT32 driver.
 *
 * The VirtIO block InternalCall reads sector data into an int[] buffer.
 * Each int holds 4 bytes in little-endian order.  These helpers extract
 * individual bytes, 16-bit words, 32-bit dwords, and 64-bit qwords from
 * such a buffer by byte offset, without any unsafe code.
 *
 * GetByte:  O(1), no division — uses shift and mask only.
 * GetWord:  two GetByte calls.
 * GetDword: four GetByte calls.
 * GetQword: two GetDword calls with sign-extension masking.
 *
 * AArch64 alignment note:
 *   The int[] element data starts at a 4-byte-aligned offset (24 bytes
 *   past the managed array header CLR_ARR_HDR_SIZE, which is 4-byte aligned).
 *   buf[idx] accesses are therefore naturally aligned for int32.
 *   All multi-byte extractions use byte arithmetic, so there are no
 *   unaligned 16-bit or 32-bit reads — safe under -mstrict-align.
 */

namespace Zapada.Fs.Fat32
{
    internal static class BufHelper
    {
        /*
         * GetByte(buf, off) - extract the byte at byte offset off from buf[].
         *
         * The int[] element at index (off >> 2) holds 4 bytes.
         * The byte's position within that element is (off & 3) * 8 bits.
         */
        internal static int GetByte(int[] buf, int off)
        {
            int idx = off >> 2;
            int sh  = (off & 3) << 3;
            int word = buf[idx];
            return (word >> sh) & 0xFF;
        }

        /*
         * GetWord(buf, off) - extract a little-endian 16-bit value at off.
         */
        internal static int GetWord(int[] buf, int off)
        {
            return GetByte(buf, off) | (GetByte(buf, off + 1) << 8);
        }

        /*
         * GetDword(buf, off) - extract a little-endian 32-bit value at off.
         *
         * Returns a signed int32.  Values with bit 31 set appear negative.
         * Use GetQword or mask with 0xFFFFFFFFL (as long) when unsigned
         * arithmetic is required.
         */
        internal static int GetDword(int[] buf, int off)
        {
            return GetByte(buf, off)
                 | (GetByte(buf, off + 1) << 8)
                 | (GetByte(buf, off + 2) << 16)
                 | (GetByte(buf, off + 3) << 24);
        }

        /*
         * GetQword(buf, off) - extract a little-endian 64-bit value at off.
         *
         * Masks the low 32 bits of each dword with 0xFFFFFFFFL to prevent
         * sign-extension when the int32 result has bit 31 set.
         */
        internal static long GetQword(int[] buf, int off)
        {
            long lo = (long)GetDword(buf, off)     & 0xFFFFFFFFL;
            long hi = (long)GetDword(buf, off + 4) & 0xFFFFFFFFL;
            return lo | (hi << 32);
        }
    }
}


