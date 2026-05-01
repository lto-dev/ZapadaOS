/*
 * Zapada - src/managed/Zapada.Boot/BootLoader.cs
 *
 * Phase 3 — managed boot driver loader (initramfs-backed).
 *
 * This class implements the top-level boot sequence for the Zapada managed
 * layer.  It is called by the kernel's C code via clr_execute_entry_point(),
 * which resolves the CLI Entry Point token and dispatches to Program.Main().
 *
 * Boot sequence (Ext4-root persistent storage discovery):
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
 *     D.4  Find and initialize Ext4 driver (Zapada.Fs.Ext4).
 *     D.5  Find and initialize VFS layer (Zapada.Fs.Vfs).
 *          Non-critical drivers: log failure but continue.
 *
 *   Phase 3 — Persistent storage discovery
 *     Use the boot-selected GPT ZAPADA_BOOT partition as the Ext4 VFS root.
 *     Validate root payloads directly from Ext4, including /etc/fstab.
 *     Mount the FAT32 ZAPADA_DATA compatibility partition at /mnt/c.
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
using Zapada.Fs.Ext;
using Zapada.Fs.Vfs;
using Zapada.Shell;
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
            Console.Write("--- Zapada.Boot ---\n");


            /* ------------------------------------------------------------ */
            /* Conformance: CLR correctness tests — run FIRST.              */
            /* ------------------------------------------------------------ */

            Console.Write("[Boot] loading: Zapada.Conformance\n");
            if (Zapada.Conformance.DllMain.Initialize() != 0)
            {
                Console.Write("[Boot] Conformance Initialize failed\n");
            }

            /* ============================================================== */
            /* Phase 1: Storage abstractions                                  */
            /* ============================================================== */

            Console.Write("[Boot] loading: Zapada.Storage\n");
            if (Zapada.Storage.DllMain.Initialize() != 0)
            {
                Console.Write("[Boot] Storage Initialize failed\n");
            }

            Zapada.Drivers.DriverRegistry.Initialize();
            RegisterKnownDriverDescriptors();

            // Phase 3: wire bootstrap RamFs root into VFS through cross-assembly
            // object reference flow (Boot -> Storage -> VFS).
            int vfsBootstrap = Vfs.Initialize();
            if (vfsBootstrap != StorageStatus.Ok)
            {
                Console.Write("[Boot] Bootstrap VFS init failed\n");
            }
            else
            {
                Console.Write("[Boot] Bootstrap VFS mounted at /\n");
            }

            /* ============================================================== */
            /* Phase 2: Pre-loaded driver initialization                      */
            /* ============================================================== */


            /* ------------------------------------------------------------ */
            /* Step 3a: find and invoke TEST.DLL (Zapada.Test.Hello).       */
            /* ------------------------------------------------------------ */

            Console.Write("[Boot] invoking: Zapada.Test.Hello\n");
            Zapada.Test.Hello.Hello.Run();

            /* ------------------------------------------------------------ */
            /* Step D.1: initialize VirtioBlock driver.                     */
            /* ------------------------------------------------------------ */

            Console.Write("[Boot] loading: Zapada.Drivers.VirtioBlock\n");
            if (Zapada.Drivers.DllMain.Initialize() == 0)
            {
                Console.Write("[Boot] VBLK Initialize failed\n");
                Zapada.Drivers.DriverRegistry.SetState("virtio-blk", Zapada.Drivers.DriverState.Failed);
            }
            else
            {
                Zapada.Drivers.DriverRegistry.SetState("virtio-blk", Zapada.Drivers.DriverState.Started);
            }

            /* ------------------------------------------------------------ */
            /* Step D.1b: initialize USB xHCI/mass-storage driver.          */
            /* ------------------------------------------------------------ */

            Console.Write("[Boot] loading: Zapada.Drivers.Usb\n");
            if (Zapada.Drivers.Usb.DllMain.Initialize() == 0)
            {
                Console.Write("[Boot] USB Initialize failed\n");
                Zapada.Drivers.DriverRegistry.SetState("xhci", Zapada.Drivers.DriverState.Failed);
                Zapada.Drivers.DriverRegistry.SetState("usb-storage", Zapada.Drivers.DriverState.Failed);
            }
            else
            {
                /* The USB manager owns final xHCI and usb-storage state. */
            }

            /* ------------------------------------------------------------ */
            /* Step D.2: initialize GPT driver.                             */
            /* ------------------------------------------------------------ */

            Console.Write("[Boot] loading: Zapada.Fs.Gpt\n");
            if (Zapada.Fs.DllMain.Initialize() == 0)
            {
                Console.Write("[Boot] GPT Initialize failed\n");
                Zapada.Drivers.DriverRegistry.SetState("gpt", Zapada.Drivers.DriverState.Failed);
            }
            else
            {
                Zapada.Drivers.DriverRegistry.SetState("gpt", Zapada.Drivers.DriverState.Started);
            }

            /* ------------------------------------------------------------ */
            /* Step D.3: initialize managed FAT32 driver.                   */
            /* ------------------------------------------------------------ */

            Console.Write("[Boot] loading: Zapada.Fs.Fat32\n");
            if (Zapada.Fs.Fat32.DllMain.Initialize() == 0)
            {
                Console.Write("[Boot] FAT32 Initialize failed\n");
                Zapada.Drivers.DriverRegistry.SetState("fat32", Zapada.Drivers.DriverState.Failed);
            }
            else
            {
                Zapada.Drivers.DriverRegistry.SetState("fat32", Zapada.Drivers.DriverState.Started);
            }

            /* ------------------------------------------------------------ */
            /* Step D.4: initialize managed Ext4 driver.                    */
            /* ------------------------------------------------------------ */

            Console.Write("[Boot] loading: Zapada.Fs.Ext4\n");
            if (Zapada.Fs.Ext4.DllMain.Initialize() == 0)
            {
                Console.Write("[Boot] Ext4 Initialize failed\n");
                Zapada.Drivers.DriverRegistry.SetState("ext4", Zapada.Drivers.DriverState.Failed);
            }
            else
            {
                Zapada.Drivers.DriverRegistry.SetState("ext4", Zapada.Drivers.DriverState.Started);
            }

            /* ------------------------------------------------------------ */
            /* Step D.5: initialize managed VFS layer.                      */
            /* ------------------------------------------------------------ */

            Console.Write("[Boot] loading: Zapada.Fs.Vfs\n");
            if (Zapada.Fs.Vfs.DllMain.Initialize() == 0)
            {
                Console.Write("[Boot] VFS Initialize failed\n");
                Zapada.Drivers.DriverRegistry.SetState("vfs", Zapada.Drivers.DriverState.Failed);
            }
            else
            {
                Zapada.Drivers.DriverRegistry.SetState("vfs", Zapada.Drivers.DriverState.Started);
            }

            int dependencyFailures = Zapada.Drivers.DriverManager.ProbeDependencies();
            if (dependencyFailures == 0)
            {
                Console.Write("[Gate] Phase-DriverGraph\n");
            }
            else
            {
                Console.Write("[Boot] Driver dependency failures= ");
                Console.Write(dependencyFailures);
                Console.Write("\n");
            }

            MountPersistentRootAndCompatibilityVolume();
            MountDeviceNamespace();
            VerifyConsoleDeviceWrite();
            MountProcNamespace();

            string bootCommandLine = BootOptions.CommandLine();
            Console.Write("[Boot] command line: ");
            Console.Write(bootCommandLine);
            Console.Write("\n");
            Console.Write("[Boot] init target: ");
            Console.Write(BootOptions.InitTarget());
            Console.Write("\n");
            Console.Write("[Boot] runlevel: ");
            Console.Write(BootOptions.Runlevel());
            Console.Write("\n");

            if (BootOptions.IsEmergencyMode())
                Console.Write("[Boot] emergency mode requested; shell fallback remains boot-owned until minid launch exists\n");
            else if (BootOptions.IsSingleUserMode())
                Console.Write("[Boot] single-user mode requested; rc1 remains pending until minid launch exists\n");
            else if (BootOptions.RequestsShellInit())
                Console.Write("[Boot] explicit shell init target requested\n");
            else if (BootOptions.RequestsMinidInit())
                Console.Write("[Boot] minid init target selected; launch substrate pending\n");
            else
                Console.Write("[Boot] unsupported init target selected; emergency fallback shell path pending\n");

            int shellRc;
            if (BootOptions.IsSmokeMode())
            {
                Console.Write("[Boot] shell mode: smoke\n");
                shellRc = ShellHost.RunBootSmoke();
            }
            else
            {
                Console.Write("[Boot] shell mode: interactive\n");
                shellRc = ShellHost.RunInteractive(-1);
            }
            if (shellRc != StorageStatus.Ok)
            {
                Console.Write("[Boot] Shell startup failed rc=");
                Console.Write(shellRc);
                Console.Write("\n");
                Zapada.Drivers.DriverRegistry.SetState("shell", Zapada.Drivers.DriverState.Failed);
            }
            else
            {
                Console.Write("[Boot] Shell startup completed\n");
                Zapada.Drivers.DriverRegistry.SetState("shell", Zapada.Drivers.DriverState.Started);
            }

            /* Gate D: boot sequence complete. */
            Console.Write("[Gate] GateD\n");
        }

        private static bool TransitionAssemblySourceToRoot()
        {
            int rc = Zapada.Runtime.InternalCalls.RuntimeTransitionToVfs("/");
            if (rc == 0)
            {
                Console.Write("[Gate] Phase-AssemblySourceVfs\n");
                return true;
            }

            Console.Write("[Boot] assembly source VFS transition failed rc=");
            Console.Write(rc);
            Console.Write("\n");
            return false;
        }

        private static void RegisterKnownDriverDescriptors()
        {
            Zapada.Drivers.DriverRegistry.Register(
                "conformance",
                "Zapada.Conformance",
                "diagnostics.conformance",
                "",
                "boot:test",
                Zapada.Drivers.DriverState.Started);

            Zapada.Drivers.DriverRegistry.Register(
                "storage",
                "Zapada.Storage",
                "storage.core,filesystem.ramfs,volume-probes",
                "",
                "boot:initramfs",
                Zapada.Drivers.DriverState.Started);

            Zapada.Drivers.DriverRegistry.Register(
                "entropy",
                "Zapada.Storage",
                "entropy.source,character.device:random",
                "",
                "boot:provisional",
                Zapada.Drivers.DriverState.Started);

            Zapada.Drivers.DriverRegistry.Register(
                "hello-test",
                "Zapada.Test.Hello",
                "diagnostics.hello",
                "Zapada.Storage",
                "boot:test",
                Zapada.Drivers.DriverState.Loaded);

            Zapada.Drivers.DriverRegistry.Register(
                "virtio-blk",
                "Zapada.Drivers.VirtioBlock",
                "block.device:vda,hal.smoke",
                "Zapada.Drivers.Hal",
                "pci:1af4:1001,pci:1af4:1042",
                Zapada.Drivers.DriverState.Loaded);

            Zapada.Drivers.DriverRegistry.Register(
                "xhci",
                "Zapada.Drivers.Usb",
                "usb.bus:xhci0",
                "Zapada.Drivers.Hal",
                "pci:class:0c0330",
                Zapada.Drivers.DriverState.Loaded);

            Zapada.Drivers.DriverRegistry.Register(
                "usb-storage",
                "Zapada.Drivers.Usb",
                "block.device:sda",
                "usb.bus:xhci0,Zapada.Storage",
                "usb:08:06:50",
                Zapada.Drivers.DriverState.Loaded);

            Zapada.Drivers.DriverRegistry.Register(
                "gpt",
                "Zapada.Fs.Gpt",
                "partition.table:gpt",
                "Zapada.Storage.PartitionView",
                "block:*",
                Zapada.Drivers.DriverState.Loaded);

            Zapada.Drivers.DriverRegistry.Register(
                "fat32",
                "Zapada.Fs.Fat32",
                "filesystem:fat32",
                "Zapada.Storage.PartitionView",
                "partition:gpt",
                Zapada.Drivers.DriverState.Loaded);

            Zapada.Drivers.DriverRegistry.Register(
                "ext4",
                "Zapada.Fs.Ext4",
                "filesystem:ext4",
                "Zapada.Storage.PartitionView",
                "partition:gpt",
                Zapada.Drivers.DriverState.Loaded);

            Zapada.Drivers.DriverRegistry.Register(
                "vfs",
                "Zapada.Fs.Vfs",
                "namespace:vfs",
                "Zapada.Storage.MountedVolume",
                "mount:*",
                Zapada.Drivers.DriverState.Loaded);

            Zapada.Drivers.DriverRegistry.Register(
                "shell",
                "Zapada.Shell",
                "shell.console",
                "Zapada.Fs.Vfs",
                "console",
                Zapada.Drivers.DriverState.Loaded);
        }

        private static void MountPersistentRootAndCompatibilityVolume()
        {
            Console.Write("[Boot] Persistent storage: scanning...\n");

            BlockDevice bootDevice = EnsureBlockDevices(0);
            if (bootDevice == null)
            {
                Console.Write("[Boot] no boot block device registered\n");
                return;
            }

            int partitionCount = PartitionScanner.ScanAllBlockDevices();
            Console.Write("[Storage] partitions discovered: ");
            Console.Write(partitionCount);
            Console.Write("\n");

            PartitionInfo rootInfo = PartitionRegistry.FindByLabel("ZAPADA_BOOT");
            if (rootInfo == null)
            {
                Console.Write("[Boot] ZAPADA_BOOT not found (non-fatal)\n");
                return;
            }

            Console.Write("[Boot] ZAPADA_BOOT at LBA ");
            Console.Write((int)rootInfo.StartLba);
            Console.Write("\n");

            BlockDevicePartitionView rootPartition = PartitionRegistry.CreateView(rootInfo);
            if (rootPartition == null)
            {
                Console.Write("[Boot] root partition view failed\n");
                return;
            }

            VolumeProbe rootProbe = DriverRegistry.FindBestProbe(rootPartition);
            if (rootProbe == null)
            {
                Console.Write("[Boot] Ext4 probe not matched (non-fatal)\n");
                return;
            }

            Console.Write("[Boot] Root probe matched: ");
            Console.Write(rootProbe.GetDriverKey());
            Console.Write("\n");

            MountedVolume rootVolume = rootProbe.Mount(rootPartition);
            if (rootVolume == null)
            {
                Console.Write("[Boot] Ext4 root mount failed (non-fatal)\n");
                return;
            }

            int rootMountRc = Vfs.MountRoot(rootVolume);
            if (rootMountRc < 0)
            {
                Console.Write("[Boot] VFS mount / failed for Ext4\n");
                return;
            }

            Console.Write("[Boot] Ext4 mounted as root /\n");
            Console.Write("[Gate] Phase-Ext4Root\n");

            VerifyExt4RootPayloads();
            MountConfiguredFstabVolumes();
            VerifyRootAssemblySourceProvider();
        }

        private static BlockDevice EnsureBlockDevices(long minimumSectorCount)
        {
            BlockDevice existing = BlockDeviceRegistry.FindByName("vda");
            if (existing != null)
                return existing;

            Console.Write("[Boot] managed vda block device not registered\n");
            return null;
        }

        private static void VerifyExt4RootPayloads()
        {
            if (VerifyMZFile("/Zapada.Boot.dll"))
                Console.Write("[Gate] Phase-Ext4Read\n");
            else
                Console.Write("[Boot] Ext4 root payload MZ verification failed\n");

            if (VerifyTextStartsWith("/etc/fstab", '#'))
                Console.Write("[Gate] Phase-Ext4Fstab\n");
            else
                Console.Write("[Boot] Ext4 /etc/fstab read failed\n");

            VerifyOrderedRootLayout();
            VerifyDiskAssemblyLoad();
        }

        private static void VerifyOrderedRootLayout()
        {
            if (!VerifyMZFile("/boot/zapada/Zapada.Boot.dll"))
            {
                Console.Write("[Boot] ordered root missing /boot/zapada/Zapada.Boot.dll\n");
                return;
            }

            if (!VerifyMZFile("/lib/dotnet/System.Private.CoreLib.dll"))
            {
                Console.Write("[Boot] ordered root missing /lib/dotnet/System.Private.CoreLib.dll\n");
                return;
            }

            if (!VerifyMZFile("/lib/zapada/Zapada.Storage.dll"))
            {
                Console.Write("[Boot] ordered root missing /lib/zapada/Zapada.Storage.dll\n");
                return;
            }

            if (!VerifyMZFile("/bin/Zapada.Shell.dll"))
            {
                Console.Write("[Boot] ordered root missing /bin/Zapada.Shell.dll\n");
                return;
            }

            Console.Write("[Gate] Phase-RootLayout\n");
        }

        private static void VerifyDiskAssemblyLoad()
        {
            byte[] image = ReadWholeFileBounded("/bin/Zapada.Test.Hello.dll", 8192);
            if (image == null)
            {
                Console.Write("[Boot] disk assembly read failed path=/bin/Zapada.Test.Hello.dll\n");
                return;
            }

            if (image.Length < 2 || image[0] != 0x4D || image[1] != 0x5A)
            {
                Console.Write("[Boot] disk assembly MZ validation failed path=/bin/Zapada.Test.Hello.dll\n");
                return;
            }

            int assemblyId = Zapada.Runtime.InternalCalls.RuntimeLoad(image);
            if (assemblyId < 0)
            {
                Console.Write("[Boot] disk assembly RuntimeLoad failed rc=");
                Console.Write(assemblyId);
                Console.Write(" path=/bin/Zapada.Test.Hello.dll\n");
                return;
            }

            Console.Write("[Boot] disk assembly loaded from /bin/Zapada.Test.Hello.dll id=");
            Console.Write(assemblyId);
            Console.Write("\n");
            Console.Write("[Gate] Phase-DiskAssemblyLoad\n");
        }

        private static void VerifyRootAssemblySourceProvider()
        {
            if (!PublishRootAssemblySet())
                return;

            if (!TransitionAssemblySourceToRoot())
                return;

            int assemblyId = Zapada.Runtime.InternalCalls.RuntimeBindFromSource("Zapada.Test.Hello");
            if (assemblyId < 0)
            {
                Console.Write("[Boot] assembly source bind failed rc=");
                Console.Write(assemblyId);
                Console.Write(" assembly=Zapada.Test.Hello\n");
                return;
            }

            Console.Write("[Boot] assembly source loaded from root assembly=Zapada.Test.Hello id=");
            Console.Write(assemblyId);
            Console.Write("\n");
            Console.Write("[Gate] Phase-AssemblySourceVfsLocate\n");

            VerifyVfsOnlyLaunchState();
        }

        private static void VerifyVfsOnlyLaunchState()
        {
            int processId = Zapada.Runtime.InternalCalls.RuntimeCreateVfsLaunchState(
                "/bin/Zapada.Test.Hello.dll",
                "Zapada.Test.Hello.Hello",
                "Run");

            if (processId < 0)
            {
                Console.Write("[Boot] VFS-only launch state failed rc=");
                Console.Write(processId);
                Console.Write("\n");
                return;
            }

            Console.Write("[Boot] VFS-only launch state process=");
            Console.Write(processId);
            Console.Write("\n");
            Console.Write("[Gate] Phase-ProcessDomain\n");

            VerifyTaskLaunch();
        }

        private static void VerifyTaskLaunch()
        {
            Console.Write("[Boot] Launching task /bin/Zapada.Test.Hello.dll\n");

            int processId = Zapada.Runtime.InternalCalls.RuntimeLaunchTask(
                "Zapada.Test.Hello",
                "Zapada.Test.Hello.Hello",
                "Run");

            if (processId < 0)
            {
                Console.Write("[Boot] Task launch failed rc=");
                Console.Write(processId);
                Console.Write("\n");
                return;
            }

            Console.Write("[Boot] Task launch completed process=");
            Console.Write(processId);
            Console.Write("\n");
        }

        private static bool PublishRootAssemblySet()
        {
            string[] assemblyPaths = new string[]
            {
                "/boot/zapada/Zapada.Boot.dll",
                "/bin/Zapada.Test.Hello.dll",
                "/bin/Zapada.Shell.dll",
                "/lib/zapada/Zapada.Storage.dll",
                "/lib/zapada/Zapada.Fs.Vfs.dll",
                "/lib/zapada/Zapada.Drivers.dll",
                "/lib/dotnet/System.Console.dll",
                "/lib/dotnet/System.dll",
                "/lib/dotnet/System.Runtime.dll",
                "/lib/dotnet/System.Private.CoreLib.dll"
            };

            for (int i = 0; i < assemblyPaths.Length; i++)
            {
                Console.Write("[Boot] assembly source publish begin path=");
                Console.Write(assemblyPaths[i]);
                Console.Write("\n");

                int rc = AssemblySourceBridge.ReadAndPublish(assemblyPaths[i]);
                if (rc != StorageStatus.Ok)
                {
                    Console.Write("[Boot] assembly source publish failed rc=");
                    Console.Write(rc);
                    Console.Write(" path=");
                    Console.Write(assemblyPaths[i]);
                    Console.Write("\n");
                    return false;
                }

                Console.Write("[Boot] assembly source publish done path=");
                Console.Write(assemblyPaths[i]);
                Console.Write("\n");
            }

            return true;
        }

        private static byte[] ReadWholeFileBounded(string path, int maxBytes)
        {
            if (path == null || path.Length == 0 || maxBytes <= 0)
                return null;

            int fd = Vfs.Open(path);
            if (fd < 0)
                return null;

            byte[] scratch = new byte[maxBytes];
            byte[] chunk = new byte[256];
            int total = 0;
            bool overflow = false;

            while (true)
            {
                int remaining = maxBytes - total;
                if (remaining <= 0)
                {
                    overflow = true;
                    break;
                }

                int request = chunk.Length;
                if (request > remaining)
                    request = remaining;

                int bytesRead = Vfs.Read(fd, chunk, 0, request);
                if (bytesRead < 0)
                {
                    Vfs.Close(fd);
                    return null;
                }

                if (bytesRead == 0)
                    break;

                for (int i = 0; i < bytesRead; i++)
                    scratch[total + i] = chunk[i];

                total += bytesRead;
            }

            Vfs.Close(fd);

            if (overflow || total <= 0)
                return null;

            byte[] exact = new byte[total];
            for (int i = 0; i < total; i++)
                exact[i] = scratch[i];

            return exact;
        }

        private static bool VerifyMZFile(string path)
        {
            int fd = Vfs.Open(path);
            if (fd < 0)
                return false;

            byte[] hdr = new byte[2];
            int nr = Vfs.Read(fd, hdr, 0, 2);
            Vfs.Close(fd);
            return nr == 2 && hdr[0] == 0x4D && hdr[1] == 0x5A;
        }

        private static bool VerifyTextStartsWith(string path, char expected)
        {
            int fd = Vfs.Open(path);
            if (fd < 0)
                return false;

            byte[] hdr = new byte[1];
            int nr = Vfs.Read(fd, hdr, 0, 1);
            Vfs.Close(fd);
            return nr == 1 && hdr[0] == expected;
        }

        private static void MountDeviceNamespace()
        {
            RegisterDeviceNodes();

            DevFsVolume devfs = new DevFsVolume();
            int mountRc = Vfs.Mount("/dev", devfs);
            if (mountRc < 0 && mountRc != StorageStatus.AlreadyExists)
            {
                Console.Write("[Boot] /dev mount failed rc=");
                Console.Write(mountRc);
                Console.Write("\n");
                return;
            }

            Console.Write("[Boot] /dev mounted from DeviceRegistry\n");
            Console.Write("[Gate] Phase-DevFs\n");
        }

        private static void VerifyConsoleDeviceWrite()
        {
            int fd = Vfs.Open("/dev/console", FileAccessIntent.ReadWrite);
            if (fd < 0)
            {
                Console.Write("[Boot] /dev/console open failed rc=");
                Console.Write(fd);
                Console.Write("\n");
                return;
            }

            string message = "[DevFs] /dev/console write through VFS\n";
            byte[] bytes = new byte[message.Length];
            for (int i = 0; i < message.Length; i++)
                bytes[i] = (byte)(message[i] & 0xFF);

            int written = Vfs.Write(fd, bytes, 0, bytes.Length);
            Vfs.Close(fd);

            if (written == bytes.Length)
            {
                Console.Write("[Gate] Phase-DevConsole\n");
                return;
            }

            Console.Write("[Boot] /dev/console write failed rc=");
            Console.Write(written);
            Console.Write("\n");
        }

        private static void RegisterDeviceNodes()
        {
            DeviceRegistry.Initialize();

            RegisterStaticDeviceNode("/dev/null", "null", DeviceKind.Null, "char:null", "devfs", FileAccessIntent.ReadWrite, 0);
            RegisterStaticDeviceNode("/dev/zero", "zero", DeviceKind.Zero, "char:zero", "devfs", FileAccessIntent.ReadOnly, 0);
            RegisterStaticDeviceNode("/dev/console", "console", DeviceKind.Console, "console:system", "shell", FileAccessIntent.ReadWrite, 0);
            EntropyService.Initialize((uint)BlockDeviceRegistry.Count(), (uint)PartitionRegistry.Count());
            RegisterStaticDeviceNode("/dev/urandom", "urandom", DeviceKind.Random, "entropy:urandom", "entropy", FileAccessIntent.ReadOnly, 1);
            RegisterStaticDeviceNode("/dev/random", "random", DeviceKind.Random, "entropy:random", "entropy", FileAccessIntent.ReadOnly, 2);

            int blockDeviceCount = BlockDeviceRegistry.Count();
            for (int i = 0; i < blockDeviceCount; i++)
            {
                BlockDevice device = BlockDeviceRegistry.Get(i);
                if (device == null)
                    continue;

                BlockDeviceInfo info = device.GetInfo();
                if (info == null || info.Name == null || info.Name.Length == 0)
                    continue;

                string path = string.Concat("/dev/", info.Name);
                string serviceKey = string.Concat("block:", info.Name);
                RegisterStaticDeviceNode(path, info.Name, DeviceKind.Block, serviceKey, info.DriverKey, FileAccessIntent.ReadWrite, i);
            }

            int partitionCount = PartitionRegistry.Count();
            for (int i = 0; i < partitionCount; i++)
            {
                PartitionInfo partition = PartitionRegistry.Get(i);
                if (partition == null)
                    continue;

                string path = string.Concat("/dev/", partition.Name);
                string serviceKey = string.Concat("partition:", partition.Name);
                RegisterStaticDeviceNode(path, partition.Name, DeviceKind.Block, serviceKey, "partition", FileAccessIntent.ReadWrite, i + 1);
            }
        }

        private static void MountProcNamespace()
        {
            BootProcFsProvider provider = new BootProcFsProvider();
            ProcFsVolume procfs = new ProcFsVolume(provider);
            int mountRc = Vfs.Mount("/proc", procfs);
            if (mountRc < 0 && mountRc != StorageStatus.AlreadyExists)
            {
                Console.Write("[Boot] /proc mount failed rc=");
                Console.Write(mountRc);
                Console.Write("\n");
                return;
            }

            Console.Write("[Boot] /proc mounted from live registry snapshots\n");
            Console.Write("[Gate] Phase-ProcFs\n");
        }

        private static void RegisterStaticDeviceNode(string path, string name, int kind, string serviceKey, string driverKey, int permissions, int targetHandle)
        {
            int rc = DeviceRegistry.RegisterNode(path, name, kind, serviceKey, driverKey, permissions, targetHandle);
            if (rc != StorageStatus.Ok && rc != StorageStatus.AlreadyExists)
            {
                Console.Write("[Boot] device node register failed: ");
                Console.Write(path);
                Console.Write(" rc=");
                Console.Write(rc);
                Console.Write("\n");
            }
        }

        private static void MountConfiguredFstabVolumes()
        {
            Console.Write("[Boot] reading: /etc/fstab\n");

            int fd = Vfs.Open("/etc/fstab");
            if (fd < 0)
            {
                Console.Write("[Boot] /etc/fstab open failed\n");
                return;
            }

            byte[] fstab = new byte[512];
            int bytesRead = Vfs.Read(fd, fstab, 0, fstab.Length);
            Vfs.Close(fd);
            if (bytesRead <= 0)
            {
                Console.Write("[Boot] /etc/fstab read failed\n");
                return;
            }

            int pos = 0;
            while (pos < bytesRead)
            {
                int lineStart = pos;
                while (pos < bytesRead && fstab[pos] != 10 && fstab[pos] != 13)
                    pos++;

                int lineEnd = pos;
                while (pos < bytesRead && (fstab[pos] == 10 || fstab[pos] == 13))
                    pos++;

                ProcessFstabLine(fstab, lineStart, lineEnd);
            }
        }

        private static void ProcessFstabLine(byte[] buffer, int start, int end)
        {
            int pos = SkipSpaces(buffer, start, end);
            if (pos >= end)
                return;
            if (buffer[pos] == '#')
                return;

            int specStart = pos;
            pos = SkipField(buffer, pos, end);
            int specLength = pos - specStart;

            pos = SkipSpaces(buffer, pos, end);
            int mountStart = pos;
            pos = SkipField(buffer, pos, end);
            int mountLength = pos - mountStart;

            pos = SkipSpaces(buffer, pos, end);
            int typeStart = pos;
            pos = SkipField(buffer, pos, end);
            int typeLength = pos - typeStart;

            if (specLength <= 0 || mountLength <= 0 || typeLength <= 0)
                return;

            if (FieldEquals(buffer, mountStart, mountLength, "/"))
                return;

            if (!FieldStartsWith(buffer, specStart, specLength, "LABEL="))
                return;

            string label = ReadAsciiField(buffer, specStart + 6, specLength - 6);
            string mountPath = ReadAsciiField(buffer, mountStart, mountLength);
            string fsType = ReadAsciiField(buffer, typeStart, typeLength);
            MountFstabVolume(label, mountPath, fsType);
        }

        private static int SkipSpaces(byte[] buffer, int pos, int end)
        {
            while (pos < end && (buffer[pos] == 32 || buffer[pos] == 9))
                pos++;

            return pos;
        }

        private static int SkipField(byte[] buffer, int pos, int end)
        {
            while (pos < end && buffer[pos] != 32 && buffer[pos] != 9)
                pos++;

            return pos;
        }

        private static bool FieldEquals(byte[] buffer, int start, int length, string value)
        {
            if (value == null || length != value.Length)
                return false;

            int i = 0;
            while (i < length)
            {
                if (buffer[start + i] != value[i])
                    return false;

                i++;
            }

            return true;
        }

        private static bool FieldStartsWith(byte[] buffer, int start, int length, string value)
        {
            if (value == null || length < value.Length)
                return false;

            int i = 0;
            while (i < value.Length)
            {
                if (buffer[start + i] != value[i])
                    return false;

                i++;
            }

            return true;
        }

        private static string ReadAsciiField(byte[] buffer, int start, int length)
        {
            string value = "";
            int i = 0;
            while (i < length)
            {
                value = string.Concat(value, ExtText.ByteToString(buffer[start + i] & 0xFF));
                i++;
            }

            return value;
        }

        private static void MountFstabVolume(string label, string mountPath, string fsType)
        {
            Console.Write("[Boot] fstab mount ");
            Console.Write(label);
            Console.Write(" -> ");
            Console.Write(mountPath);
            Console.Write(" type=");
            Console.Write(fsType);
            Console.Write("\n");

            PartitionInfo info = PartitionRegistry.FindByLabel(label);
            if (info == null)
            {
                Console.Write("[Boot] fstab partition not found\n");
                return;
            }

            BlockDevicePartitionView fstabPartition = PartitionRegistry.CreateView(info);
            if (fstabPartition == null)
            {
                Console.Write("[Boot] fstab partition view failed\n");
                return;
            }

            string driverKey = MapFstabTypeToDriverKey(fsType);
            MountedVolume fstabVolume = MountKnownFstabVolume(fstabPartition, driverKey);
            if (fstabVolume == null)
            {
                Console.Write("[Boot] fstab mount failed during probe mount\n");
                return;
            }

            Console.Write("[Boot] Compatibility probe matched: ");
            Console.Write(driverKey);
            Console.Write("\n");

            int mountRc = Vfs.Mount(mountPath, fstabVolume);
            if (mountRc < 0)
            {
                Console.Write("[Boot] VFS fstab mount failed\n");
                return;
            }

            Console.Write("[Boot] fstab mounted: ");
            Console.Write(mountPath);
            Console.Write("\n");

            int listRc = Vfs.List(mountPath);
            if (listRc < 0)
                Console.Write("[Boot] fstab VFS list failed\n");

            if (mountPath == "/mnt/c" && VerifyMZFile("/mnt/c/TEST.DLL"))
            {
                Console.Write("[Boot] FAT32 VFS read OK: MZ verified\n");
                Console.Write("[Gate] Phase-Fat32Driver\n");
                Console.Write("[Gate] Phase-Fat32MntC\n");
            }
            else if (mountPath == "/mnt/d" && VerifyMZFile("/mnt/d/TEST.DLL"))
            {
                Console.Write("[Boot] second disk FAT32 read OK: MZ verified\n");
                Console.Write("[Gate] Phase-Fat32MntD\n");
                Console.Write("[Gate] Phase-MultiDiskStorage\n");
            }
            else if (mountPath == "/mnt/u" && VerifyMZFile("/mnt/u/TEST.DLL"))
            {
                Console.Write("[Boot] USB FAT32 read OK: MZ verified\n");
                Console.Write("[Gate] Phase-Fat32MntU\n");
            }
            else if (mountPath == "/mnt/c" || mountPath == "/mnt/d" || mountPath == "/mnt/u")
            {
                Console.Write("[Boot] FAT32 VFS read failed on mounted TEST.DLL\n");
            }
            else
            {
                Console.Write("[Boot] fstab mount verified without file smoke\n");
            }
        }

        private static string MapFstabTypeToDriverKey(string fsType)
        {
            if (fsType == "vfat" || fsType == "fat" || fsType == "fat32")
                return "fat32";

            if (fsType == "ext4" || fsType == "ext3" || fsType == "ext2")
                return "ext4";

            return fsType;
        }

        private static MountedVolume MountKnownFstabVolume(PartitionView partition, string driverKey)
        {
            if (driverKey == "fat32")
                return Zapada.Fs.Fat32.Fat32Driver.Mount(partition);

            VolumeProbe probe = DriverRegistry.FindBestProbe(partition);
            if (probe == null || probe.GetDriverKey() != driverKey)
                return null;

            return probe.Mount(partition);
        }
    }
}
