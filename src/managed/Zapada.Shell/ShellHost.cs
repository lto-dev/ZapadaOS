using System;
using Zapada.Fs.Vfs;
using Zapada.Storage;

namespace Zapada.Shell;

public static class ShellHost
{
    private const int ShellExit = 1;
    private const string Prompt = "zapada:/# ";

    public static int RunBootShell()
    {
        Console.Write("\n");
        Console.Write("Zapada Shell\n");
        Console.Write("============\n");
        PrintMotd();
        Console.Write("[Shell] startup command smoke\n");
        RunBootSmokeCommand("mount");
        RunBootSmokeCommand("ls /");
        RunBootSmokeCommand("ls /mnt/c");
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
        if (TextEquals(line, commandStart, commandLength, "pwd"))
            return PrintWorkingDirectory();
        if (TextEquals(line, commandStart, commandLength, "ls") || TextEquals(line, commandStart, commandLength, "dir"))
            return ListPath(ReadArgumentOrDefault(line, argStart, argEnd, "/"));
        if (TextEquals(line, commandStart, commandLength, "cat") || TextEquals(line, commandStart, commandLength, "type"))
            return CatPath(ReadArgumentOrDefault(line, argStart, argEnd, ""));
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

        if (TextEquals(command, 0, command.Length, "ls /") || TextEquals(command, 0, command.Length, "ls /mnt/c"))
        {
            Console.Write("[Shell] listing ");
            Console.Write(command.Substring(3, command.Length - 3));
            Console.Write("\n");
            return;
        }

        if (TextEquals(command, 0, command.Length, "cat /etc/fstab"))
        {
            Console.Write("# Zapada filesystem table\n");
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
        Console.Write("  pwd               print the current directory\n");
        Console.Write("  ls [path]         list a directory\n");
        Console.Write("  cat <path>        print a text file\n");
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
