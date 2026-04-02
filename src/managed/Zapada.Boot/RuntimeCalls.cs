/*
 * Zapada - src/managed/Zapada.Boot/RuntimeCalls.cs
 *
 * InternalCall bridge for managed assembly loading.
 *
 * Full type name: "Zapada.Runtime.InternalCalls"
 * Dispatched through internalcall_table in internalcall.c.
 *
 * Phase 3B:
 *   RuntimeLoad()       -> ic_runtime_load()        in internalcall.c
 *   RuntimeCallMethod() -> ic_runtime_call_method()  in internalcall.c
 *
 * Phase 3:
 *   RuntimeFindByName() -> ic_runtime_find_by_name() in internalcall.c
 *
 */

using System.Runtime.CompilerServices;

namespace Zapada.Runtime
{
    internal static class InternalCalls
    {
        /*
         * RuntimeLoad(byte[] dll) -> int
         *
         * Parse and register an additional PE assembly from a raw byte array.
         * Returns the 0-based assembly slot index on success, -1 on failure
         * (table full or parse error).
         *
         * Argument kinds in InternalCall table:
         *   arg[0] = CLR_IC_KIND_OBJREF (dll, a managed byte[] array)
         * Return kind: CLR_IC_KIND_I32 (slot index)
         */
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern int RuntimeLoad(byte[] dll);

        /*
         * RuntimeCallMethod(string qualifiedTypeName, string methodName, int slot) -> int
         *
         * Looks up the type by qualified name (namespace split at last dot) in
         * the assembly at the given slot index, finds the method by name, and
         * executes it via clr_interpret().
         *
         * Returns 1 on success, 0 on failure (type/method not found, or execution
         * error).
         *
         * Argument kinds in InternalCall table:
         *   arg[0] = CLR_IC_KIND_OBJREF (qualifiedTypeName)
         *   arg[1] = CLR_IC_KIND_OBJREF (methodName)
         *   arg[2] = CLR_IC_KIND_I32    (slot, the value returned by RuntimeLoad)
         * Return kind: CLR_IC_KIND_I32 (1=success, 0=failure)
         */
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern int RuntimeCallMethod(
            string qualifiedTypeName,
            string methodName,
            int    slot);

        /*
         * RuntimeFindByName(string name) -> int
         *
         * Phase 3: scan the assembly registry for a loaded assembly whose
         * pe_get_assembly_name() result matches <name>.
         *
         * Returns the 0-based slot index on success, -1 if not found.
         *
         * Used by BootLoader.cs to discover assemblies pre-loaded from the
         * initramfs ramdisk without having to read them from FAT32.
         *
         * Argument kinds in InternalCall table:
         *   arg[0] = CLR_IC_KIND_OBJREF (name)
         * Return kind: CLR_IC_KIND_I32 (slot index or -1)
         */
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern int RuntimeFindByName(string name);
    }
}


