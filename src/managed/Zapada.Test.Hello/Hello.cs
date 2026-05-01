/*
 * Zapada - src/managed/Zapada.Test.Hello/Hello.cs
 *
 * Minimal managed task fixture for ZACLR process launch validation.
 *
 * Hello.Run() is invoked as the entry point of a non-boot managed task
 * launched through zaclr_runtime_launch_task(). Its output confirms
 * end-to-end task launch: separate process, separate domain, VFS-backed
 * assembly loading, entry point resolution, and managed execution.
 */

using System;

namespace Zapada.Test.Hello
{
    public static class Hello
    {
        public static void Run()
        {
            Console.Write("[Gate] Phase3B\n");
            Console.Write("[Task] Zapada.Test.Hello running in separate domain\n");
            Console.Write("[Gate] Phase-TaskLaunch\n");
        }
    }
}



