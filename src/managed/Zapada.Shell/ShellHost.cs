using System;
using Zapada.Fs.Vfs;
using Zapada.Storage;

namespace Zapada.Shell;

public static class ShellHost
{
    private const int ShellExit = 1;
    private const string Prompt = "zapada:/# ";

    public static int RunBootSmoke()
    {
        Console.Write("\n");
        Console.Write("Zapada Shell\n");
        Console.Write("============\n");
        PrintMotd();
        Console.Write("[Shell] startup command smoke\n");
        RunBootSmokeCommand("mount");
        RunBootSmokeCommand("drivers");
        RunBootSmokeCommand("block");
        RunBootSmokeCommand("partitions");
        RunBootSmokeCommand("ls /");
        RunBootSmokeCommand("ls /mnt/c");
        RunBootSmokeCommand("ls /mnt/d");
        RunBootSmokeCommand("ls /dev");
        RunBootSmokeCommand("ls /proc");
        RunBootSmokeCommand("cat /proc/mounts");
        RunBootSmokeCommand("cat /proc/drivers");
        RunBootSmokeCommand("entropy");
        RunBootSmokeCommand("cat /etc/fstab");
        Console.Write("[Gate] Phase-Shell\n");
        PrintPrompt();
        Console.Write("\n");
        return StorageStatus.Ok;
    }

    public static int RunInteractive(int maxCommands)
    {
        PrintMotd();

        int count = 0;
        while (maxCommands < 0 || count < maxCommands)
        {
            PrintPrompt();
            string line = Console.ReadLine();
            if (line == null)
                return StorageStatus.Ok;

            int rc = Execute(line);
            if (rc == ShellExit)
                return StorageStatus.Ok;

            count++;
        }

        return StorageStatus.Ok;
    }

    public static int Execute(string line)
    {
        if (line == null)
            return StorageStatus.InvalidArgument;

        int length = line.Length;
        int commandStart = SkipSpaces(line, 0, length);
        if (commandStart >= length)
            return StorageStatus.Ok;

        int commandEnd = commandStart;
        while (commandEnd < length && line[commandEnd] != ' ' && line[commandEnd] != '\t')
            commandEnd++;

        int commandLength = commandEnd - commandStart;
        int argStart = SkipSpaces(line, commandEnd, length);
        int argEnd = TrimRight(line, argStart, length);

        if (TextEquals(line, commandStart, commandLength, "help"))
            return PrintHelp();
        if (TextEquals(line, commandStart, commandLength, "motd"))
            return PrintMotd();
        if (TextEquals(line, commandStart, commandLength, "mount") || TextEquals(line, commandStart, commandLength, "mounts"))
            return Vfs.PrintMounts();
        if (TextEquals(line, commandStart, commandLength, "drivers") || TextEquals(line, commandStart, commandLength, "lsmod"))
            return Zapada.Drivers.DriverRegistry.PrintDrivers();
        if (TextEquals(line, commandStart, commandLength, "drivers-full"))
            return Zapada.Drivers.DriverRegistry.PrintDriversFull();
        if (TextEquals(line, commandStart, commandLength, "services"))
            return Zapada.Drivers.DriverRegistry.PrintServices();
        if (TextEquals(line, commandStart, commandLength, "block") || TextEquals(line, commandStart, commandLength, "blocks") || TextEquals(line, commandStart, commandLength, "lsblk"))
            return BlockDeviceRegistry.PrintDevices();
        if (TextEquals(line, commandStart, commandLength, "partitions") || TextEquals(line, commandStart, commandLength, "parts"))
            return PartitionRegistry.PrintPartitions();
        if (TextEquals(line, commandStart, commandLength, "modprobe"))
            return StartDriver(ReadArgumentOrDefault(line, argStart, argEnd, ""));
        if (TextEquals(line, commandStart, commandLength, "driver"))
            return ExecuteDriverCommand(ReadArgumentOrDefault(line, argStart, argEnd, ""));
        if (TextEquals(line, commandStart, commandLength, "modprobe-plan"))
            return Zapada.Drivers.DriverManager.PrintDependencyPlan();
        if (TextEquals(line, commandStart, commandLength, "pwd"))
            return PrintWorkingDirectory();
        if (TextEquals(line, commandStart, commandLength, "ls") || TextEquals(line, commandStart, commandLength, "dir"))
            return ListPath(ReadArgumentOrDefault(line, argStart, argEnd, "/"));
        if (TextEquals(line, commandStart, commandLength, "cat") || TextEquals(line, commandStart, commandLength, "type"))
            return CatPath(ReadArgumentOrDefault(line, argStart, argEnd, ""));
        if (TextEquals(line, commandStart, commandLength, "entropy") || TextEquals(line, commandStart, commandLength, "random"))
            return PrintEntropy(ReadArgumentOrDefault(line, argStart, argEnd, "/dev/urandom"));
        if (TextEquals(line, commandStart, commandLength, "clear") || TextEquals(line, commandStart, commandLength, "cls"))
            return ClearScreen();
        if (TextEquals(line, commandStart, commandLength, "exit") || TextEquals(line, commandStart, commandLength, "halt"))
            return ShellExit;

        Console.Write("[Shell] unknown command: ");
        Console.Write(line.Substring(commandStart, commandLength));
        Console.Write("\n");
        return StorageStatus.NotFound;
    }

    private static void RunBootCommand(string command)
    {
        Console.Write("[Shell] $ ");
        Console.Write(command);
        Console.Write("\n");
        Execute(command);
    }

    private static void RunBootSmokeCommand(string command)
    {
        Console.Write("[Shell] $ ");
        Console.Write(command);
        Console.Write("\n");

        if (TextEquals(command, 0, command.Length, "mount"))
        {
            Vfs.PrintMounts();
            return;
        }

        if (TextEquals(command, 0, command.Length, "drivers"))
        {
            Zapada.Drivers.DriverRegistry.PrintDrivers();
            return;
        }

        if (TextEquals(command, 0, command.Length, "block"))
        {
            BlockDeviceRegistry.PrintDevices();
            return;
        }

        if (TextEquals(command, 0, command.Length, "partitions"))
        {
            PartitionRegistry.PrintPartitions();
            return;
        }

        Execute(command);
    }

    private static int PrintHelp()
    {
        Console.Write("Commands:\n");
        Console.Write("  help              show this command list\n");
        Console.Write("  motd              print /etc/motd or the built-in message\n");
        Console.Write("  mount, mounts     list VFS mount points\n");
        Console.Write("  drivers, lsmod    list loaded driver descriptors\n");
        Console.Write("  drivers-full      list driver dependencies and bind targets\n");
        Console.Write("  services          list registered service descriptors\n");
        Console.Write("  block, lsblk      list registered block devices\n");
        Console.Write("  partitions        list registered partitions\n");
        Console.Write("  modprobe <driver> start a registered driver descriptor\n");
        Console.Write("  driver tree       show dependency-provider graph\n");
        Console.Write("  driver start <d>  start a registered driver descriptor\n");
        Console.Write("  modprobe-plan     show dependency readiness\n");
        Console.Write("  pwd               print the current directory\n");
        Console.Write("  ls [path]         list a directory\n");
        Console.Write("  cat <path>        print a text file\n");
        Console.Write("  entropy [device]  read 16 random bytes as hex\n");
        Console.Write("  clear             add terminal spacing\n");
        Console.Write("  exit, halt        leave the shell loop\n");
        return StorageStatus.Ok;
    }

    private static int PrintMotd()
    {
        int rc = PrintFile("/etc/motd", true);
        if (rc >= 0)
            return StorageStatus.Ok;

        Console.Write("Welcome to Zapada.\n");
        Console.Write("Root is mounted from Ext4 at /; FAT32 compatibility is mounted at /mnt/c.\n");
        return StorageStatus.Ok;
    }

    private static int PrintWorkingDirectory()
    {
        Console.Write("/\n");
        return StorageStatus.Ok;
    }

    private static int StartDriver(string driverKey)
    {
        if (driverKey == null || driverKey.Length == 0)
        {
            Console.Write("[Shell] modprobe needs a driver key\n");
            return StorageStatus.InvalidArgument;
        }

        int rc = Zapada.Drivers.DriverManager.Start(driverKey);
        Console.Write("[Shell] modprobe ");
        Console.Write(driverKey);
        Console.Write(" rc=");
        Console.Write(rc);
        Console.Write("\n");
        return rc == 0 ? StorageStatus.Ok : StorageStatus.NotFound;
    }

    private static int ExecuteDriverCommand(string args)
    {
        if (args == null || args.Length == 0)
        {
            Console.Write("[Shell] driver needs a subcommand\n");
            return StorageStatus.InvalidArgument;
        }

        int length = args.Length;
        int commandStart = SkipSpaces(args, 0, length);
        if (commandStart >= length)
        {
            Console.Write("[Shell] driver needs a subcommand\n");
            return StorageStatus.InvalidArgument;
        }

        int commandEnd = commandStart;
        while (commandEnd < length && args[commandEnd] != ' ' && args[commandEnd] != '\t')
            commandEnd++;

        int commandLength = commandEnd - commandStart;
        int argStart = SkipSpaces(args, commandEnd, length);
        int argEnd = TrimRight(args, argStart, length);

        if (TextEquals(args, commandStart, commandLength, "tree"))
            return Zapada.Drivers.DriverManager.PrintDriverTree();

        if (TextEquals(args, commandStart, commandLength, "start"))
            return StartDriver(ReadArgumentOrDefault(args, argStart, argEnd, ""));

        Console.Write("[Shell] unknown driver subcommand: ");
        Console.Write(args.Substring(commandStart, commandLength));
        Console.Write("\n");
        return StorageStatus.NotFound;
    }

    private static int ListPath(string path)
    {
        if (path == null || path.Length == 0)
            path = "/";

        Console.Write("[Shell] listing ");
        Console.Write(path);
        Console.Write("\n");

        int rc = Vfs.List(path, new ShellListSink());
        if (rc < 0)
        {
            Console.Write("[Shell] ls failed rc=");
            Console.Write(rc);
            Console.Write(" path=");
            Console.Write(path);
            Console.Write("\n");
        }

        return rc;
    }

    private static int CatPath(string path)
    {
        if (path == null || path.Length == 0)
        {
            Console.Write("[Shell] cat needs a path\n");
            return StorageStatus.InvalidArgument;
        }

        int rc = PrintFile(path, false);
        if (rc >= 0)
            Console.Write("\n");

        return rc;
    }

    private static int PrintEntropy(string path)
    {
        if (path == null || path.Length == 0)
            path = "/dev/urandom";

        int fd = Vfs.Open(path);
        if (fd < 0)
        {
            Console.Write("[Shell] entropy open failed rc=");
            Console.Write(fd);
            Console.Write(" path=");
            Console.Write(path);
            Console.Write("\n");
            return fd;
        }

        byte[] buffer = new byte[16];
        int bytesRead = Vfs.Read(fd, buffer, 0, buffer.Length);
        Vfs.Close(fd);

        if (bytesRead < 0)
        {
            Console.Write("[Shell] entropy read failed rc=");
            Console.Write(bytesRead);
            Console.Write(" path=");
            Console.Write(path);
            Console.Write("\n");
            return bytesRead;
        }

        Console.Write("[Shell] entropy source: ");
        Console.Write(path);
        Console.Write(" (provisional non-cryptographic)\n");
        Console.Write("[Shell] entropy bytes:");
        for (int i = 0; i < bytesRead; i++)
        {
            Console.Write(" ");
            WriteHexByte(buffer[i] & 0xFF);
        }
        Console.Write("\n");
        Console.Write("[Gate] Phase-Entropy\n");
        return StorageStatus.Ok;
    }

    private static int PrintFile(string path, bool quietMissing)
    {
        int fd = Vfs.Open(path);
        if (fd < 0)
        {
            if (!quietMissing)
            {
                Console.Write("[Shell] open failed rc=");
                Console.Write(fd);
                Console.Write(" path=");
                Console.Write(path);
                Console.Write("\n");
            }

            return fd;
        }

        byte[] buffer = new byte[128];
        int finalRc = StorageStatus.Ok;
        while (true)
        {
            int bytesRead = Vfs.Read(fd, buffer, 0, buffer.Length);
            if (bytesRead < 0)
            {
                finalRc = bytesRead;
                break;
            }

            if (bytesRead == 0)
                break;

            for (int i = 0; i < bytesRead; i++)
                WriteTextByte(buffer[i] & 0xFF);
        }

        Vfs.Close(fd);

        if (finalRc < 0 && !quietMissing)
        {
            Console.Write("[Shell] read failed rc=");
            Console.Write(finalRc);
            Console.Write(" path=");
            Console.Write(path);
            Console.Write("\n");
        }

        return finalRc;
    }

    private static int ClearScreen()
    {
        Console.Write("\n\n\n\n\n\n\n\n");
        return StorageStatus.Ok;
    }

    private static void PrintPrompt()
    {
        Console.Write(Prompt);
    }

    private static void WriteTextByte(int value)
    {
        if (value == 10)
        {
            Console.Write("\n");
            return;
        }

        if (value == 13)
            return;

        if (value == 9)
        {
            Console.Write("\t");
            return;
        }

        if (value >= 32 && value <= 126)
        {
            Console.Write((char)value);
            return;
        }

        Console.Write(".");
    }

    private static void WriteHexByte(int value)
    {
        WriteHexNibble((value >> 4) & 0xF);
        WriteHexNibble(value & 0xF);
    }

    private static void WriteHexNibble(int value)
    {
        if (value < 10)
        {
            Console.Write((char)('0' + value));
            return;
        }

        Console.Write((char)('a' + (value - 10)));
    }

    private static string ReadArgumentOrDefault(string line, int argStart, int argEnd, string defaultValue)
    {
        if (argStart >= argEnd)
            return defaultValue;

        return line.Substring(argStart, argEnd - argStart);
    }

    private static int SkipSpaces(string text, int start, int end)
    {
        int pos = start;
        while (pos < end && (text[pos] == ' ' || text[pos] == '\t'))
            pos++;

        return pos;
    }

    private static int TrimRight(string text, int start, int end)
    {
        int pos = end;
        while (pos > start && (text[pos - 1] == ' ' || text[pos - 1] == '\t'))
            pos--;

        return pos;
    }

    private static bool TextEquals(string text, int start, int length, string expected)
    {
        if (text == null || expected == null || length != expected.Length)
            return false;

        for (int i = 0; i < length; i++)
        {
            char left = text[start + i];
            char right = expected[i];
            if (left >= 'A' && left <= 'Z')
                left = (char)(left + 32);
            if (right >= 'A' && right <= 'Z')
                right = (char)(right + 32);
            if (left != right)
                return false;
        }

        return true;
    }

    private sealed class ShellListSink : DirectoryEntrySink
    {
        public override void OnEntry(int nodeHandle, string name, int nodeKind)
        {
            Console.Write("  ");
            if (nodeKind == 2)
                Console.Write("<dir>  ");
            else if (nodeKind == 1)
                Console.Write("<file> ");
            else
                Console.Write("<node> ");

            Console.Write(name);
            Console.Write("\n");
        }
    }
}
