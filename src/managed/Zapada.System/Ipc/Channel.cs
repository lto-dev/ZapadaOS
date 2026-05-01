/*
 * Zapada - src/managed/Zapada.System/Ipc/Channel.cs
 *
 * Named IPC channel API for service communication.
 *
 * A service process creates a channel with a well-known name:
 *   int handle = Channel.Create("sys/vfs");
 *
 * Client processes open a channel by name:
 *   int handle = Channel.Open("sys/vfs");
 *
 * Client sends a request and reads the reply:
 *   Channel.Send(handle, requestBytes, 0, requestBytes.Length);
 *   int n = Channel.Receive(handle, replyBuffer, 0, replyBuffer.Length);
 *
 * Service receives a request and sends a reply:
 *   int n = Channel.Receive(handle, requestBuffer, 0, requestBuffer.Length);
 *   int sender = Channel.GetSenderPid(handle);
 *   Channel.Reply(handle, replyBytes, 0, replyBytes.Length);
 *
 * Both sides close when done:
 *   Channel.Close(handle);
 */

namespace Zapada.System.Ipc;

public static class Channel
{
    /// <summary>
    /// Create a named service channel. Only the service process calls this.
    /// Returns a channel handle (>= 0) or negative error.
    /// </summary>
    public static int Create(string name)
    {
        return NativeCalls.SysChannelCreate(name);
    }

    /// <summary>
    /// Open a channel by service name. Client processes call this.
    /// Returns a channel handle (>= 0) or negative error.
    /// </summary>
    public static int Open(string name)
    {
        return NativeCalls.SysChannelOpen(name);
    }

    /// <summary>
    /// Send a request message on an open channel.
    /// Returns 0 on success, negative error.
    /// </summary>
    public static int Send(int channel, byte[] data, int offset, int count)
    {
        return NativeCalls.SysChannelSend(channel, data, offset, count);
    }

    /// <summary>
    /// Receive a request message (service side) or reply (client side).
    /// Writes into buffer starting at offset. Returns bytes received
    /// (>= 0) or negative error. Blocks if no message is pending.
    /// </summary>
    public static int Receive(int channel, byte[] buffer, int offset, int capacity)
    {
        return NativeCalls.SysChannelReceive(channel, buffer, offset, capacity);
    }

    /// <summary>
    /// Send a reply back to the last request sender on a service channel.
    /// Returns 0 on success, negative error.
    /// </summary>
    public static int Reply(int channel, byte[] data, int offset, int count)
    {
        return NativeCalls.SysChannelReply(channel, data, offset, count);
    }

    /// <summary>
    /// Close a channel handle. Returns 0 on success, negative error.
    /// </summary>
    public static int Close(int channel)
    {
        return NativeCalls.SysChannelClose(channel);
    }

    /// <summary>
    /// Get the sender pid of the last received message on this channel.
    /// Returns the pid (>= 0) or negative error.
    /// </summary>
    public static int GetSenderPid(int channel)
    {
        return NativeCalls.SysChannelGetSenderPid(channel);
    }
}
