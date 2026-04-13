/*
 * Zapada - src/managed/Zapada.Test.Hello/Hello.cs
 *
 * Phase 3B test fixture: loaded from ZAPADA_BOOT/TEST.DLL at runtime by
 * BootLoader.cs via RuntimeLoad + RuntimeCallMethod.
 *
 * Hello.Run() is invoked after the DLL is loaded into the assembly registry.
 * Its output confirms the end-to-end path from disk read to managed execution.
 *
 * Phase 3B boot scaffolding — replaced by managed VFS in Phase 3.1.
 */

using System;

namespace Zapada.Test.Hello
{
    public static class Hello
    {
        /* Phase 3B boot scaffolding — replaced by managed VFS in Phase 3.1 */
        public static void Run()
        {
            Console.Write("[Boot] Zapada.Test.Hello loaded\n");
            Console.Write("[Gate] Phase3B\n");
        }
    }
}



