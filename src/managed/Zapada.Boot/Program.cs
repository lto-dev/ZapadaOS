/*
 * Zapada - src/managed/Zapada.Boot/Program.cs
 *
 * Phase 3A Part 3 — managed assembly entry point.
 *
 * OutputType=Exe in Zapada.Boot.csproj ensures the C# compiler sets the
 * CLI Entry Point token in the PE metadata to point at Program.Main().
 * The kernel's clr_execute_entry_point() resolves this token, locates the
 * CIL method body, and dispatches to the CIL interpreter.
 *
 * All boot work is delegated to BootLoader.Run() to keep Program.cs minimal.
 */

using System;

namespace Zapada.Boot
{
    internal static class Program
    {
        /*
         * Main - CLI entry point.
         *
         * The CIL interpreter calls this method directly after resolving the
         * Entry Point token from the PE CLI header.
         * No args; no return value.
         */
        static void Main()
        {
            Console.Write("--- Zapada.Boot.Main ---\n");
            BootLoader.Run();
        }
    }
}


