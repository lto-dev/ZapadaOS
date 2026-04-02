/*
 * Zapada - src/managed/Zapada.Boot/BootLoader.cs
 *
 * Phase 3 — managed boot driver loader (initramfs-backed).
 *
 * This class implements the top-level boot sequence for the Zapada managed
 * layer.  It is called by the kernel's C code via clr_execute_entry_point(),
 * which resolves the CLI Entry Point token and dispatches to Program.Main().
 *
 * Boot sequence (restructured for non-fatal persistent storage discovery):
 *
 *   Phase 1 — Storage abstractions
 *     S.1  Find and initialize Zapada.Storage (STORAGE.DLL).
 *          This emits [Gate] Phase-Storage from Storage's DllMain.
 *          CRITICAL: if Storage is missing, bail out.
 *
 *   Phase 2 — Pre-loaded driver initialization
 *     Conf Find and initialize Conformance (Zapada.Conformance) FIRST.
 *          CLR correctness tests run before any other driver code.
 *     3a   Find and invoke TEST.DLL (Zapada.Test.Hello).
 *     D.1  Find and initialize VBLK driver (Zapada.Drivers.VirtioBlock).
 *     D.2  Find and initialize GPT driver (Zapada.Fs.Gpt).
 *     D.3  Find and initialize FAT32 driver (Zapada.Fs.Fat32).
 *     D.4  Find and initialize VFS layer (Zapada.Fs.Vfs).
 *          Non-critical drivers: log failure but continue.
 *
 *   Phase 3 — Persistent storage discovery (NON-FATAL)
 *     Try GPT scan to find ZAPADA_BOOT partition.
 *     If found: mount FAT32 volume, call SetBootPartLba.
 *     If not found: print informational message, continue.
 *
 *   [Gate] GateD — always emitted at the end.
 *
 * Gate output format checked by test-all.ps1:
 *   "[Boot] Conformance OK"            -- CLR conformance passed
 *   "[Gate] Phase3.2-Conf"             -- conformance gate
 *   "[Boot] Zapada.Test.Hello loaded"  -- Hello.Run() log from the temporary fixture
 *   "[Boot] invoking: Zapada.Test.Hello" -- BootLoader call-site log (avoids duplicate text)
 *   "[Gate] Phase3B"                   -- Hello.Run() executed successfully
 *   "[Gate] GateD"                     -- Gate D complete
 */
using System;
using Zapada.Fs.Vfs;
using Zapada.Storage;

namespace Zapada.Boot
{
    internal static class BootLoader
    {
        /*
         * Run - top-level boot sequence.
         *
         * Called from Program.Main().
         * Prints gate lines to serial for test-all.ps1 verification.
         */
        internal static void Run()
        {
            System.Console.Write("--- Zapada.Boot ---\n");


            /* ------------------------------------------------------------ */
            /* Conformance: CLR correctness tests — run FIRST.              */
            /* ------------------------------------------------------------ */

            int confSlot = Zapada.Runtime.InternalCalls.RuntimeFindByName("Zapada.Conformance");
            if (confSlot < 0)
            {
                System.Console.Write("[Boot] Zapada.Conformance not pre-loaded\n");
            }
            else
            {
                System.Console.Write("[Boot] found: Zapada.Conformance\n");
                System.Console.Write("[Boot] Zapada.Conformance loaded\n");

                int confInit = Zapada.Runtime.InternalCalls.RuntimeCallMethod(
                    "Zapada.Conformance.DllMain", "Initialize", confSlot);
                if (confInit == 0)
                {
                    System.Console.Write("[Boot] Conformance Initialize failed\n");
                }
            }

            /* ============================================================== */
            /* Phase 1: Storage abstractions                                  */
            /* ============================================================== */

            int storageSlot = Zapada.Runtime.InternalCalls.RuntimeFindByName("Zapada.Storage");
            if (storageSlot < 0)
            {
                System.Console.Write("[Boot] Zapada.Storage not pre-loaded\n");
                System.Console.Write("[Gate] GateD\n");
                return;
            }
            System.Console.Write("[Boot] found: Zapada.Storage\n");
            System.Console.Write("[Boot] Zapada.Storage loaded\n");

            int storageInit = Zapada.Runtime.InternalCalls.RuntimeCallMethod(
                "Zapada.Storage.DllMain", "Initialize", storageSlot);
            if (storageInit == 0)
            {
                System.Console.Write("[Boot] Storage Initialize failed\n");
            }

            // Phase 3: wire bootstrap RamFs root into VFS through cross-assembly
            // object reference flow (Boot -> Storage -> VFS).
            int vfsBootstrap = Vfs.Initialize();
            if (vfsBootstrap != StorageStatus.Ok)
            {
                System.Console.Write("[Boot] Bootstrap VFS init failed\n");
            }
            else
            {
                System.Console.Write("[Boot] Bootstrap VFS mounted at /\n");
            }

            /* ============================================================== */
            /* Phase 2: Pre-loaded driver initialization                      */
            /* ============================================================== */


            /* ------------------------------------------------------------ */
            /* Step 3a: find and invoke TEST.DLL (Zapada.Test.Hello).       */
            /* ------------------------------------------------------------ */

            int testSlot = Zapada.Runtime.InternalCalls.RuntimeFindByName("Zapada.Test.Hello");
            if (testSlot < 0)
            {
                System.Console.Write("[Boot] Zapada.Test.Hello not pre-loaded\n");
            }
            else
            {
                System.Console.Write("[Boot] invoking: Zapada.Test.Hello\n");
                int callResult = Zapada.Runtime.InternalCalls.RuntimeCallMethod(
                    "Zapada.Test.Hello.Hello", "Run", testSlot);
                if (callResult == 0)
                {
                    System.Console.Write("[Boot] RuntimeCallMethod failed\n");
                }
            }

            /* ------------------------------------------------------------ */
            /* Step D.1: initialize VirtioBlock driver.                     */
            /* ------------------------------------------------------------ */

            int vblkSlot = Zapada.Runtime.InternalCalls.RuntimeFindByName("Zapada.Drivers.VirtioBlock");
            if (vblkSlot < 0)
            {
                System.Console.Write("[Boot] Zapada.Drivers.VirtioBlock not pre-loaded\n");
            }
            else
            {
                System.Console.Write("[Boot] found: Zapada.Drivers.VirtioBlock\n");
                System.Console.Write("[Boot] Zapada.Drivers.VirtioBlock loaded\n");

                int vblkInit = Zapada.Runtime.InternalCalls.RuntimeCallMethod(
                    "Zapada.Drivers.DllMain", "Initialize", vblkSlot);
                if (vblkInit == 0)
                {
                    System.Console.Write("[Boot] VBLK Initialize failed\n");
                }
            }

            /* ------------------------------------------------------------ */
            /* Step D.2: initialize GPT driver.                             */
            /* ------------------------------------------------------------ */

            int gptSlot = Zapada.Runtime.InternalCalls.RuntimeFindByName("Zapada.Fs.Gpt");
            if (gptSlot < 0)
            {
                System.Console.Write("[Boot] Zapada.Fs.Gpt not pre-loaded\n");
            }
            else
            {
                System.Console.Write("[Boot] found: Zapada.Fs.Gpt\n");
                System.Console.Write("[Boot] Zapada.Fs.Gpt loaded\n");

                int gptInit = Zapada.Runtime.InternalCalls.RuntimeCallMethod(
                    "Zapada.Fs.DllMain", "Initialize", gptSlot);
                if (gptInit == 0)
                {
                    System.Console.Write("[Boot] GPT Initialize failed\n");
                }
            }

            /* ------------------------------------------------------------ */
            /* Step D.3: initialize managed FAT32 driver.                   */
            /* ------------------------------------------------------------ */

            int fat32Slot = Zapada.Runtime.InternalCalls.RuntimeFindByName("Zapada.Fs.Fat32");
            if (fat32Slot < 0)
            {
                System.Console.Write("[Boot] Zapada.Fs.Fat32 not pre-loaded\n");
            }
            else
            {
                System.Console.Write("[Boot] found: Zapada.Fs.Fat32\n");
                System.Console.Write("[Boot] Zapada.Fs.Fat32 loaded\n");

                int fat32Init = Zapada.Runtime.InternalCalls.RuntimeCallMethod(
                    "Zapada.Fs.Fat32.DllMain", "Initialize", fat32Slot);
                if (fat32Init == 0)
                {
                    System.Console.Write("[Boot] FAT32 Initialize failed\n");
                }
            }

            /* ------------------------------------------------------------ */
            /* Step D.4: initialize managed VFS layer.                      */
            /* ------------------------------------------------------------ */

            int vfsSlot = Zapada.Runtime.InternalCalls.RuntimeFindByName("Zapada.Fs.Vfs");
            if (vfsSlot < 0)
            {
                System.Console.Write("[Boot] Zapada.Fs.Vfs not pre-loaded\n");
            }
            else
            {
                System.Console.Write("[Boot] found: Zapada.Fs.Vfs\n");
                System.Console.Write("[Boot] Zapada.Fs.Vfs loaded\n");

                int vfsInit = Zapada.Runtime.InternalCalls.RuntimeCallMethod(
                    "Zapada.Fs.Vfs.DllMain", "Initialize", vfsSlot);
                if (vfsInit == 0)
                {
                    System.Console.Write("[Boot] VFS Initialize failed\n");
                }
            }

            /* ============================================================== */
            /* Phase 3: Persistent storage discovery (NON-FATAL)              */
            /* ============================================================== */

            System.Console.Write("[Boot] Persistent storage: scanning...\n");

            int startLba = Gpt.FindZapadaBootPartition();
            if (startLba < 0)
            {
                System.Console.Write("[Boot] ZAPADA_BOOT not found (non-fatal)\n");
            }
            else
            {
                System.Console.Write("[Boot] ZAPADA_BOOT at LBA ");
                Zapada.Console.WriteInt(startLba);
                System.Console.Write("\n");

                VirtioPartitionView partition = new VirtioPartitionView();
                partition.Initialize(startLba, 65536, 2);

                VolumeProbe? bestProbe = DriverRegistry.FindBestProbe(partition);
                if (bestProbe == null)
                {
                    System.Console.Write("[Boot] No filesystem probe matched (non-fatal)\n");
                }
                else
                {
                    System.Console.Write("[Boot] Probe matched: ");
                    System.Console.Write(bestProbe.GetDriverKey());
                    System.Console.Write("\n");

                    MountedVolume? mountedVolume = bestProbe.Mount(partition);
                    if (mountedVolume == null)
                    {
                        System.Console.Write("[Boot] Probe mount failed (non-fatal)\n");
                    }
                    else
                    {
                        int mountRc = Vfs.Mount("/boot", mountedVolume);
                        if (mountRc < 0)
                        {
                            System.Console.Write("[Boot] VFS mount /boot failed\n");
                        }
                        else
                        {
                            System.Console.Write("[Boot] FAT32 mounted at /boot\n");

                            int listRc = Vfs.List("/boot");
                            if (listRc < 0)
                            {
                                System.Console.Write("[Boot] FAT32 VFS list failed\n");
                            }

                            int fd = Vfs.Open("/boot/TEST.DLL");
                            if (fd >= 0)
                            {
                                byte[] hdr = new byte[2];
                                int nr = Vfs.Read(fd, hdr, 0, 2);
                                Vfs.Close(fd);

                                if (nr == 2 && hdr[0] == 0x4D && hdr[1] == 0x5A)
                                {
                                    System.Console.Write("[Boot] FAT32 VFS read OK: MZ verified\n");
                                    System.Console.Write("[Gate] Phase-Fat32Driver\n");
                                }
                                else
                                {
                                    System.Console.Write("[Boot] FAT32 VFS read: MZ mismatch\n");
                                }
                            }
                            else
                            {
                                System.Console.Write("[Boot] FAT32 VFS open failed\n");
                            }
                        }
                    }
                }
            }

            /* Gate D: boot sequence complete. */
            System.Console.Write("[Gate] GateD\n");
        }
    }
}
