/*
 * Zapada - src/managed/Zapada.System/Ipc/NativeCalls.cs
 *
 * Native InternalCall declarations for the IPC channel subsystem.
 * These map to native implementations in src/libraries/Zapada.System/
 * via the ZACLR native registry.
 *
 * The IPC layer provides named service channels. A service process
 * creates a channel with a well-known name (e.g. "sys/vfs"), and
 * client processes open channels by name to send requests and
 * receive replies.
 */

using System.Runtime.CompilerServices;

namespace Zapada.System.Ipc;

internal static class NativeCalls
{
    /// <summary>
    /// Create a named service channel owned by the current process.
    /// Returns a channel handle (>= 0) on success, negative error code on failure.
    /// </summary>
    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int SysChannelCreate(string name);

    /// <summary>
    /// Open a channel by service name. Returns a channel handle (>= 0)
    /// on success, negative error code if the service does not exist.
    /// </summary>
    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int SysChannelOpen(string name);

    /// <summary>
    /// Send request bytes on an open channel. Returns 0 on success,
    /// negative error code on failure. Blocks if the channel buffer is full.
    /// </summary>
    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int SysChannelSend(int channel, byte[] data, int offset, int count);

    /// <summary>
    /// Receive request bytes from a service channel (service side).
    /// Writes into buffer starting at offset. Returns bytes received
    /// (>= 0) or negative error code. Blocks if no request is pending.
    /// senderPid is set to the caller's process id by the kernel.
    /// </summary>
    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int SysChannelReceive(int channel, byte[] buffer, int offset, int capacity);

    /// <summary>
    /// Send a reply back to the last request sender on a service channel.
    /// Returns 0 on success, negative error code on failure.
    /// </summary>
    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int SysChannelReply(int channel, byte[] data, int offset, int count);

    /// <summary>
    /// Close a channel handle. The handle becomes invalid after this call.
    /// Returns 0 on success, negative error code on failure.
    /// </summary>
    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int SysChannelClose(int channel);

    /// <summary>
    /// Get the sender pid of the last received message on a service channel.
    /// Returns the pid (>= 0) or negative error if no message has been received.
    /// </summary>
    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern int SysChannelGetSenderPid(int channel);
}
