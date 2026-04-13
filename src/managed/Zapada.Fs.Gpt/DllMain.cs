/*
 * Zapada - src/managed/Zapada.Fs.Gpt/DllMain.cs
 *
 * Phase 3.1 D.2: GPT filesystem driver entry point.
 *
 * DllMain.Initialize() is invoked by BootLoader.cs via RuntimeCallMethod
 * after GPT.DLL is loaded from the ZAPADA_BOOT FAT32 partition.
 *
 * This is the Phase 3.1 D.2 gate for managed GPT driver loading.
 * Initialize() announces the driver and emits the gate line, then returns 1.
 *
 * Gate output checked by test-all.ps1:
 *   "[Gate] Phase31-D2"  — GPT managed driver DllMain executed successfully
 *
 * The driver registers itself here.  Full GPT partition table reading and
 * CRC32 header validation are in GptReader.cs.  In Phase 3.1 D.2 the reader
 * is initialized but only emits the gate; no BootLoader API calls are made
 * back to the native layer from within Initialize().  Cross-layer calls are
 * the subject of Phase 3.1 D.3+ (FAT32 managed driver) and D.4 (VFS).
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



