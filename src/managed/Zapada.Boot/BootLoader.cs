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
            MountProcNamespace();

            string bootCommandLine = BootOptions.CommandLine();
            Console.Write("[Boot] command line: ");
            Console.Write(bootCommandLine);
            Console.Write("\n");

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

            BlockDevice bootDevice = EnsureNativeBlockDevices(0);
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
        }

        private static BlockDevice EnsureNativeBlockDevices(long minimumSectorCount)
        {
            BlockDevice existing = BlockDeviceRegistry.FindByName("vda");
            if (existing != null)
                return existing;

            int nativeCount = Zapada.BlockDev.DeviceCount();
            if (nativeCount <= 0)
            {
                long fallbackSectors = Zapada.BlockDev.SectorCount();
                if (fallbackSectors <= 0)
                    fallbackSectors = minimumSectorCount;
                if (fallbackSectors < 0)
                    fallbackSectors = 0;
                if (fallbackSectors > 0)
                    nativeCount = 1;
            }

            int registered = 0;
            for (int i = 0; i < nativeCount; i++)
            {
                string name = NativeBlockDeviceName(i);
                long sectorCount = Zapada.BlockDev.SectorCountForDevice(i);
                if (i == 0 && sectorCount <= 0)
                    sectorCount = Zapada.BlockDev.SectorCount();
                if (sectorCount <= 0)
                    sectorCount = minimumSectorCount;
                if (sectorCount < 0)
                    sectorCount = 0;

                NativeBridgeBlockDevice bridge = new NativeBridgeBlockDevice(name, "virtio-blk", 512, sectorCount, i);
                int rc = BlockDeviceRegistry.Register(bridge);
                if (rc == StorageStatus.Ok || rc == StorageStatus.AlreadyExists)
                {
                    Zapada.Drivers.DriverRegistry.AddUse("virtio-blk");
                    Console.Write("[Storage] block registered: ");
                    Console.Write(name);
                    Console.Write(" native-bridge\n");
                    registered++;
                }
                else
                {
                    Console.Write("[Storage] block register failed rc=");
                    Console.Write(rc);
                    Console.Write("\n");
                }
            }

            if (registered > 0)
            {
                Console.Write("[Gate] Phase-BlockRegistry\n");
                return BlockDeviceRegistry.FindByName("vda");
            }

            return null;
        }

        private static string NativeBlockDeviceName(int index)
        {
            if (index == 0) return "vda";
            if (index == 1) return "vdb";
            if (index == 2) return "vdc";
            if (index == 3) return "vdd";
            return "vdx";
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
                RegisterStaticDeviceNode(path, info.Name, DeviceKind.Block, serviceKey, "virtio-blk", FileAccessIntent.ReadWrite, i);
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

            BlockDevice bootDevice = BlockDeviceRegistry.FindByName("vda");
            if (bootDevice == null)
            {
                Console.Write("[Boot] fstab block device missing\n");
                return;
            }

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
            else if (mountPath == "/mnt/c" || mountPath == "/mnt/d")
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
