using System;
using Zapada.Storage;

namespace Zapada.Fs.Vfs;

/// <summary>
/// Static VFS coordinator. Provides Open/Read/Close operations through the
/// mounted-volume abstraction.
/// </summary>
public static class Vfs
{
    private static bool s_initialized = false;

    /// <summary>
    /// Initialize the VFS layer and mount the bootstrap root volume as '/'.
    /// </summary>
    public static int Initialize()
    {
        Console.Write("[VFS] Initialize.enter\n");
        if (s_initialized)
        {
            Console.Write("[VFS] Initialize.already\n");
            return StorageStatus.Ok;
        }

        // Initialize/reset VFS state explicitly at boot. Static constructor
        // support now exists, but boot still uses an explicit initialization
        // sequence for deterministic ordering.
        MountTable.Initialize();
        FdTable.Initialize();
        Console.Write("[VFS] Initialize.tables-ready\n");

        RamFsVolume bootstrapRoot = new RamFsVolume();
        bootstrapRoot.InitializeRamFs();

        int rootSlot = MountTable.Mount("/", bootstrapRoot);
        Console.Write("[VFS] Initialize.mount-root= ");
        Console.Write(rootSlot);
        Console.Write("\n");
        if (rootSlot < 0)
            return rootSlot;

        s_initialized = true;
        Console.Write("[VFS] Initialize.mount-count= ");
        Console.Write(MountTable.Count);
        Console.Write("\n");
        return StorageStatus.Ok;
    }

    /// <summary>
    /// Mount an additional volume at the given absolute mount path.
    /// </summary>
    public static int Mount(string mountPath, MountedVolume volume)
    {
        if (!s_initialized)
            return StorageStatus.NotMounted;

        return MountTable.Mount(mountPath, volume);
    }

    public static int MountRoot(MountedVolume volume)
    {
        if (!s_initialized)
            return StorageStatus.NotMounted;

        return MountTable.ReplaceRoot(volume);
    }

    /// <summary>
    /// Open a file by absolute path. Returns a file descriptor index, or -1 on failure.
    /// </summary>
    public static int Open(string path)
    {
        string localPath;

        Console.Write("[VFS] Open.enter\n");

        if (!s_initialized)
        {
            Console.Write("[VFS] Open.not-mounted\n");
            return StorageStatus.NotMounted;
        }

        Console.Write("[VFS] Open.resolve\n");
        int slot = PathResolver.Resolve(path);
        if (slot < 0)
        {
            Console.Write("[VFS] Open.resolve-failed\n");
            return StorageStatus.NotFound;
        }

        Console.Write("[VFS] Open.get-volume\n");
        MountedVolume? volume = MountTable.GetVolume(slot);
        if (volume == null)
        {
            Console.Write("[VFS] Open.volume-null\n");
            return StorageStatus.NotMounted;
        }

        string? mountPath = MountTable.GetPath(slot);
        if (mountPath == null)
            return StorageStatus.NotMounted;

        if (mountPath.Length == 1 && mountPath[0] == '/')
        {
            localPath = path;
        }
        else if (path.Length == mountPath.Length)
        {
            localPath = "/";
        }
        else if (path.Length > mountPath.Length && path[mountPath.Length] == '/')
        {
            localPath = path.Substring(mountPath.Length, path.Length - mountPath.Length);
        }
        else
        {
            return StorageStatus.NotFound;
        }

        Console.Write("[VFS] Open.resolve-node\n");
        Console.Write("[VFS] Open.localPath=");
        Console.Write(localPath);
        Console.Write("\n");
        int nodeHandle = volume.Resolve(localPath);
        if (nodeHandle < 0)
        {
            Console.Write("[VFS] Open.node-not-found\n");
            return nodeHandle;
        }

        Console.Write("[VFS] Open.volume-open\n");
        int token = volume.Open(nodeHandle, FileAccessIntent.ReadOnly);
        if (token < 0)
        {
            Console.Write("[VFS] Open.volume-open-failed\n");
            return token;
        }

        NodeFacts facts = new NodeFacts();
        int size = 0;
        int statRc = volume.Stat(nodeHandle, facts);
        if (statRc >= 0)
        {
            size = (int)facts.Size;
            Console.Write("[VFS] Open.stat-ok\n");
        }
        else
        {
            Console.Write("[VFS] Open.stat-failed\n");
        }

        Console.Write("[VFS] Open.alloc-fd\n");
        int fd = FdTable.Alloc(slot, token, size);
        if (fd < 0)
        {
            Console.Write("[VFS] Open.alloc-fd-failed\n");
            volume.Close(token);
            return StorageStatus.TableFull;
        }

        Console.Write("[VFS] Open.ok\n");
        return fd;
    }

    /// <summary>
    /// Close a file descriptor. Returns 0 on success, -1 on invalid fd.
    /// </summary>
    public static int Close(int fd)
    {
        if (!FdTable.IsOpen(fd))
            return StorageStatus.InvalidArgument;

        int slot = FdTable.MountSlot(fd);
        MountedVolume? volume = MountTable.GetVolume(slot);
        if (volume == null)
            return StorageStatus.NotMounted;

        int token = FdTable.VolumeToken(fd);
        int rc = volume.Close(token);
        if (rc < 0)
            return rc;

        FdTable.Free(fd);
        return StorageStatus.Ok;
    }

    /// <summary>
    /// Read from an open file descriptor into a managed byte array.
    /// </summary>
    public static int Read(int fd, byte[] buffer, int offset, int count)
    {
        if (!FdTable.IsOpen(fd))
            return StorageStatus.InvalidArgument;

        if (buffer == null || offset < 0 || count <= 0)
            return StorageStatus.InvalidArgument;

        if (offset + count > buffer.Length)
            return StorageStatus.InvalidArgument;

        int slot = FdTable.MountSlot(fd);
        MountedVolume? volume = MountTable.GetVolume(slot);
        if (volume == null)
            return StorageStatus.NotMounted;

        int token = FdTable.VolumeToken(fd);
        int bytesRead = volume.Read(token, buffer, offset, count);

        return bytesRead;
    }

    public static int List(string path)
    {
        string localPath;

        if (!s_initialized)
        {
            return StorageStatus.NotMounted;
        }

        int slot = PathResolver.Resolve(path);
        if (slot < 0)
        {
            return StorageStatus.NotFound;
        }

        MountedVolume? volume = MountTable.GetVolume(slot);
        if (volume == null)
            return StorageStatus.NotMounted;

        string? mountPath = MountTable.GetPath(slot);
        if (mountPath == null)
            return StorageStatus.NotMounted;

        if (mountPath.Length == 1 && mountPath[0] == '/')
        {
            localPath = path;
        }
        else if (path.Length == mountPath.Length)
        {
            localPath = "/";
        }
        else if (path.Length > mountPath.Length && path[mountPath.Length] == '/')
        {
            localPath = path.Substring(mountPath.Length, path.Length - mountPath.Length);
        }
        else
        {
            return StorageStatus.NotFound;
        }

        int nodeHandle = volume.Resolve(localPath);
        if (nodeHandle < 0)
            return nodeHandle;

        return volume.ListDirectory(nodeHandle, new DebugListSink());
    }

    private sealed class DebugListSink : DirectoryEntrySink
    {
        public override void OnEntry(int nodeHandle, string name, int nodeKind)
        {
            Console.Write("[VFS] entry: ");
            Console.Write(name);
            Console.Write("\n");
        }
    }

    public static bool IsInitialized => s_initialized;
    public static int MountCount => MountTable.Count;
}

