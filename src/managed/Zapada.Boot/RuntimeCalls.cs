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

    }
}


