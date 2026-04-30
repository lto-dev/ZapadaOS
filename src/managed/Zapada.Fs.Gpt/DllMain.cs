/*
 * Zapada - src/managed/Zapada.Fs.Gpt/DllMain.cs
 *
 * Phase 3.1 D.2: GPT filesystem driver entry point.
 *
 * DllMain.Initialize() is invoked by BootLoader.cs via RuntimeCallMethod
 * after the managed boot flow loads the GPT driver payload.
 *
 * This is the Phase 3.1 D.2 gate for managed GPT driver loading.
 * Initialize() announces the driver and emits the gate line, then returns 1.
 *
 * Gate output checked by test-all.ps1:
 *   "[Gate] Phase31-D2"  — GPT managed driver DllMain executed successfully
 *
 * GPT partition discovery now runs through Zapada.Storage.PartitionScanner over
 * registered managed block devices. This assembly remains as the GPT driver
 * identity/gate while the active scanner is shared by all block backends.
 */

using System;

namespace Zapada.Fs
{
    public static class DllMain
    {
        /*
         * Initialize() - called by BootLoader.cs once GPT.DLL is loaded.
         *
         * Returns 1 on success, 0 on failure.
         */
        public static int Initialize()
        {
            Console.Write("[Boot] GPT driver initialized\n");
            Console.Write("[Gate] Phase31-D2\n");
            return 1;
        }
    }
}



