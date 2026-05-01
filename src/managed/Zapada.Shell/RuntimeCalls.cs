/*
 * Zapada - src/managed/Zapada.Shell/RuntimeCalls.cs
 *
 * InternalCall bridge declarations accessible from the shell.
 * These map to the same native handlers as the identical declarations
 * in Zapada.Boot/RuntimeCalls.cs. The ZACLR native registry resolves
 * InternalCalls by full type name + method signature regardless of
 * which assembly declares them.
 */

using System.Runtime.CompilerServices;

namespace Zapada.Runtime
{
    internal static class ShellInternalCalls
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern int RuntimeLaunchTask(string imagePath, string entryType, string entryMethod);
    }
}
