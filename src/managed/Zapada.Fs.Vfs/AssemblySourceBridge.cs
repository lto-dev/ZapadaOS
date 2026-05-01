using System;
using Zapada.Storage;

namespace Zapada.Fs.Vfs;

public static class AssemblySourceBridge
{
    private const int MaxAssemblyBytes = 32 * 1024 * 1024;
    private const int ChunkBytes = 64 * 1024;

    public static int ReadAndPublish(string path)
    {
        return ReadAndPublishBounded(path, MaxAssemblyBytes);
    }

    private static int ReadAndPublishBounded(string path, int maxBytes)
    {
        if (path == null || path.Length == 0 || maxBytes <= 0)
            return StorageStatus.InvalidArgument;

        int fd = Vfs.Open(path);
        if (fd < 0)
        {
            Console.Write("[AssemblySourceBridge] open failed path=");
            Console.Write(path);
            Console.Write(" rc=");
            Console.Write(fd);
            Console.Write("\n");
            return fd;
        }

        Console.Write("[AssemblySourceBridge] open ok path=");
        Console.Write(path);
        Console.Write(" fd=");
        Console.Write(fd);
        Console.Write("\n");

        int fileSize = Vfs.Size(fd);
        Console.Write("[AssemblySourceBridge] size path=");
        Console.Write(path);
        Console.Write(" size=");
        Console.Write(fileSize);
        Console.Write("\n");

        if (fileSize <= 0 || fileSize > maxBytes)
        {
            Console.Write("[AssemblySourceBridge] invalid size path=");
            Console.Write(path);
            Console.Write(" size=");
            Console.Write(fileSize);
            Console.Write(" max=");
            Console.Write(maxBytes);
            Console.Write("\n");
            Vfs.Close(fd);
            return StorageStatus.NotFound;
        }

        Console.Write("[AssemblySourceBridge] publish begin call path=");
        Console.Write(path);
        Console.Write(" size=");
        Console.Write(fileSize);
        Console.Write("\n");

        int beginRc = Runtime.InternalCalls.PublishBegin(path, fileSize);
        if (beginRc != 0)
        {
            Console.Write("[AssemblySourceBridge] publish begin failed path=");
            Console.Write(path);
            Console.Write(" rc=");
            Console.Write(beginRc);
            Console.Write(" size=");
            Console.Write(fileSize);
            Console.Write("\n");
            Vfs.Close(fd);
            return beginRc;
        }

        Console.Write("[AssemblySourceBridge] publish begin ok path=");
        Console.Write(path);
        Console.Write("\n");

        byte[] chunk = new byte[ChunkBytes];
        int total = 0;
        int chunkIndex = 0;
        int nextProgress = ChunkBytes * 4;
        bool sawHeader = false;
        int startTick = Runtime.InternalCalls.GetTickCount();

        while (total < fileSize)
        {
            int request = ChunkBytes;
            int remaining = fileSize - total;
            if (request > remaining)
                request = remaining;

            int t0 = Runtime.InternalCalls.GetTickCount();
            int bytesRead = Vfs.Read(fd, chunk, 0, request);
            int t1 = Runtime.InternalCalls.GetTickCount();
            if (bytesRead < 0)
            {
                Console.Write("[AssemblySourceBridge] read failed path=");
                Console.Write(path);
                Console.Write(" rc=");
                Console.Write(bytesRead);
                Console.Write(" chunk=");
                Console.Write(chunkIndex);
                Console.Write(" total=");
                Console.Write(total);
                Console.Write(" request=");
                Console.Write(request);
                Console.Write(" size=");
                Console.Write(fileSize);
                Console.Write("\n");
                Vfs.Close(fd);
                return bytesRead;
            }

            if (bytesRead == 0)
            {
                Console.Write("[AssemblySourceBridge] short read path=");
                Console.Write(path);
                Console.Write(" chunk=");
                Console.Write(chunkIndex);
                Console.Write(" total=");
                Console.Write(total);
                Console.Write(" request=");
                Console.Write(request);
                Console.Write(" size=");
                Console.Write(fileSize);
                Console.Write("\n");
                break;
            }

            if (!sawHeader)
            {
                if (bytesRead < 2 || chunk[0] != 0x4D || chunk[1] != 0x5A)
                {
                    Console.Write("[AssemblySourceBridge] invalid header path=");
                    Console.Write(path);
                    Console.Write(" bytes=");
                    Console.Write(bytesRead);
                    Console.Write("\n");
                    Vfs.Close(fd);
                    return StorageStatus.CorruptedData;
                }

                sawHeader = true;
            }

            int t2 = Runtime.InternalCalls.GetTickCount();
            int appendRc = Runtime.InternalCalls.PublishAppend(path, chunk, bytesRead);
            int t3 = Runtime.InternalCalls.GetTickCount();
            if (appendRc != 0)
            {
                Console.Write("[AssemblySourceBridge] publish append failed path=");
                Console.Write(path);
                Console.Write(" rc=");
                Console.Write(appendRc);
                Console.Write(" chunk=");
                Console.Write(chunkIndex);
                Console.Write(" total=");
                Console.Write(total);
                Console.Write(" bytes=");
                Console.Write(bytesRead);
                Console.Write(" size=");
                Console.Write(fileSize);
                Console.Write("\n");
                Vfs.Close(fd);
                return appendRc;
            }

            total += bytesRead;
            chunkIndex++;
            if (total >= nextProgress || total == fileSize)
            {
                int readMs = t1 - t0;
                int appendMs = t3 - t2;
                Console.Write("[AssemblySourceBridge] progress path=");
                Console.Write(path);
                Console.Write(" chunk=");
                Console.Write(chunkIndex);
                Console.Write(" total=");
                Console.Write(total);
                Console.Write(" size=");
                Console.Write(fileSize);
                int elapsedMs = Runtime.InternalCalls.GetTickCount() - startTick;
                Console.Write(" readMs=");
                Console.Write(readMs);
                Console.Write(" appendMs=");
                Console.Write(appendMs);
                Console.Write(" elapsedMs=");
                Console.Write(elapsedMs);
                Console.Write("\n");
                nextProgress += ChunkBytes * 4;
            }
        }

        Console.Write("[AssemblySourceBridge] close path=");
        Console.Write(path);
        Console.Write(" total=");
        Console.Write(total);
        Console.Write(" size=");
        Console.Write(fileSize);
        Console.Write("\n");
        Vfs.Close(fd);

        if (total != fileSize)
        {
            Console.Write("[AssemblySourceBridge] total mismatch path=");
            Console.Write(path);
            Console.Write(" total=");
            Console.Write(total);
            Console.Write(" size=");
            Console.Write(fileSize);
            Console.Write("\n");
            return StorageStatus.IoError;
        }

        int endRc = Runtime.InternalCalls.PublishEnd(path);
        if (endRc != 0)
        {
            Console.Write("[AssemblySourceBridge] publish end failed path=");
            Console.Write(path);
            Console.Write(" rc=");
            Console.Write(endRc);
            Console.Write(" size=");
            Console.Write(fileSize);
            Console.Write("\n");
            return endRc;
        }

        return StorageStatus.Ok;
    }

    private static void LogChunkCheckpoint(string phase, string path, int chunkIndex, int total, int request, int fileSize, int result)
    {
        if (chunkIndex < 4 || (chunkIndex % 4) == 0)
        {
            Console.Write("[AssemblySourceBridge] ");
            Console.Write(phase);
            Console.Write(" path=");
            Console.Write(path);
            Console.Write(" chunk=");
            Console.Write(chunkIndex);
            Console.Write(" total=");
            Console.Write(total);
            Console.Write(" request=");
            Console.Write(request);
            Console.Write(" size=");
            Console.Write(fileSize);
            Console.Write(" result=");
            Console.Write(result);
            Console.Write("\n");
        }
    }
}
