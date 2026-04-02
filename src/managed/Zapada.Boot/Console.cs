/*
 * Zapada - src/managed/Zapada.Boot/Console.cs
 *
 * InternalCall bridge for kernel serial console output.
 *
 * Full type name: "Zapada.Console"
 * Methods dispatched through internalcall_table in internalcall.c.
 *
 * The interpreter converts ldstr strings from the #US heap to
 * null-terminated ASCII before passing them to Write().
 */

using System.Runtime.CompilerServices;

namespace Zapada
{
    internal static class Console
    {
        /*
         * WriteInt(int) - write the decimal representation of a signed 32-bit
         * integer to the kernel console.
         */
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void WriteInt(int n);

        /*
         * WriteHex(int) - write the low byte of n as exactly 2 upper-case hex
         * digits (no "0x" prefix).
         */
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void WriteHex(int n);
    }
}


