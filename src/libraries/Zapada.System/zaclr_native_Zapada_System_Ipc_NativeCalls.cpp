/*
 * Zapada - src/libraries/Zapada.System/zaclr_native_Zapada_System_Ipc_NativeCalls.cpp
 *
 * Native InternalCall implementations for Zapada.System.Ipc.NativeCalls.
 *
 * This file implements named IPC service channels built on top of the
 * existing kernel IPC ring buffer primitives (ipc.h). The named channel
 * registry maps string names to IPC handles, allowing service discovery
 * by name (e.g. "sys/vfs", "sys/proc").
 *
 * Named channel table:
 *   - Fixed-size static array of named_channel_entry slots.
 *   - Each slot maps a name string to an underlying ipc_handle_t.
 *   - Service channels track owner pid, pending request sender pid,
 *     and request/reply buffers.
 *
 * Protocol:
 *   Service creates channel -> named_channel_entry allocated.
 *   Client opens by name -> finds entry, returns channel index as handle.
 *   Client sends request -> bytes copied to channel request buffer, flag set.
 *   Service receives -> reads request buffer, returns bytes.
 *   Service replies -> bytes copied to channel reply buffer, flag set.
 *   Client completes -> reads reply buffer.
 */

#include "zaclr_native_Zapada_System_Ipc_NativeCalls.h"

#include <kernel/zaclr/include/zaclr_public_api.h>
#include <kernel/zaclr/exec/zaclr_interop_dispatch.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/heap/zaclr_array.h>

extern "C" {
#include <kernel/console.h>
}

/* ---------------------------------------------------------------------- */
/* Named channel registry                                                  */
/* ---------------------------------------------------------------------- */

#define NAMED_CHANNEL_MAX       16u
#define NAMED_CHANNEL_NAME_MAX  64u
#define NAMED_CHANNEL_BUF_MAX   4096u

enum named_channel_state {
    NCHAN_FREE    = 0,
    NCHAN_OPEN    = 1,
    NCHAN_CLOSED  = 2,
};

struct named_channel_entry {
    enum named_channel_state state;
    char name[NAMED_CHANNEL_NAME_MAX];

    uint32_t owner_pid;

    /* Request buffer: client writes, service reads. */
    uint8_t  request_buffer[NAMED_CHANNEL_BUF_MAX];
    uint32_t request_length;
    uint32_t request_sender_pid;
    volatile uint32_t has_request;

    /* Reply buffer: service writes, client reads. */
    uint8_t  reply_buffer[NAMED_CHANNEL_BUF_MAX];
    uint32_t reply_length;
    volatile uint32_t has_reply;
};

static struct named_channel_entry s_named_channels[NAMED_CHANNEL_MAX];
static bool s_named_channels_initialized = false;

static void ensure_initialized(void)
{
    if (s_named_channels_initialized)
        return;
    for (uint32_t i = 0u; i < NAMED_CHANNEL_MAX; ++i)
    {
        s_named_channels[i].state = NCHAN_FREE;
        s_named_channels[i].name[0] = '\0';
        s_named_channels[i].owner_pid = 0u;
        s_named_channels[i].request_length = 0u;
        s_named_channels[i].request_sender_pid = 0u;
        s_named_channels[i].has_request = 0u;
        s_named_channels[i].reply_length = 0u;
        s_named_channels[i].has_reply = 0u;
    }
    s_named_channels_initialized = true;
}

/* Simple ASCII string compare. */
static bool str_eq(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0')
    {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

/* Copy count bytes from src to dst. */
static void mem_copy(void* dst, const void* src, uint32_t count)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0u; i < count; ++i)
        d[i] = s[i];
}

/* Convert a managed string to ASCII in a static buffer, return pointer. */
static const char* string_to_ascii(const struct zaclr_string_desc* str, char* buf, uint32_t buf_size)
{
    if (str == NULL)
    {
        buf[0] = '\0';
        return buf;
    }

    const uint16_t* chars = zaclr_string_chars(str);
    uint32_t len = zaclr_string_length(str);
    if (len >= buf_size) len = buf_size - 1u;
    for (uint32_t i = 0u; i < len; ++i)
        buf[i] = (char)(chars[i] & 0x7F);
    buf[len] = '\0';
    return buf;
}

/* Find a named channel by name. Returns index or -1. */
static int32_t find_by_name(const char* name)
{
    for (uint32_t i = 0u; i < NAMED_CHANNEL_MAX; ++i)
    {
        if (s_named_channels[i].state == NCHAN_OPEN && str_eq(s_named_channels[i].name, name))
            return (int32_t)i;
    }
    return -1;
}

/* Find a free slot. Returns index or -1. */
static int32_t find_free_slot(void)
{
    for (uint32_t i = 0u; i < NAMED_CHANNEL_MAX; ++i)
    {
        if (s_named_channels[i].state == NCHAN_FREE)
            return (int32_t)i;
    }
    return -1;
}

/* Get the current process pid from the runtime.
   Uses boot_process_id as the default. When child processes
   execute InternalCalls, their domain context will carry their
   own process id once per-domain pid tracking is added. */
static uint32_t get_current_pid(struct zaclr_runtime* runtime)
{
    if (runtime == NULL) return 0u;
    return (uint32_t)runtime->state.boot_process_id;
}

/* ---------------------------------------------------------------------- */
/* SysChannelCreate: create a named service channel.                       */
/* Args: (string name) -> int                                              */
/* ---------------------------------------------------------------------- */

struct zaclr_result zaclr_native_Zapada_System_Ipc_NativeCalls::SysChannelCreate___STATIC__I4__STRING(struct zaclr_native_call_frame& frame)
{
    ensure_initialized();

    if (frame.runtime == NULL)
        return zaclr_native_call_frame_set_i4(&frame, -1);

    const struct zaclr_string_desc* name_str;
    struct zaclr_result status = zaclr_native_call_frame_arg_string(&frame, 0u, &name_str);
    if (status.status != ZACLR_STATUS_OK || name_str == NULL)
        return zaclr_native_call_frame_set_i4(&frame, -2);

    static char name_buf[NAMED_CHANNEL_NAME_MAX];
    string_to_ascii(name_str, name_buf, NAMED_CHANNEL_NAME_MAX);

    if (name_buf[0] == '\0')
        return zaclr_native_call_frame_set_i4(&frame, -3);

    /* Check for duplicate name. */
    if (find_by_name(name_buf) >= 0)
    {
        console_write("[IPC] channel already exists: ");
        console_write(name_buf);
        console_write("\n");
        return zaclr_native_call_frame_set_i4(&frame, -4);
    }

    int32_t slot = find_free_slot();
    if (slot < 0)
    {
        console_write("[IPC] channel table full\n");
        return zaclr_native_call_frame_set_i4(&frame, -5);
    }

    struct named_channel_entry* ch = &s_named_channels[slot];
    ch->state = NCHAN_OPEN;
    uint32_t name_len = 0u;
    while (name_buf[name_len] != '\0' && name_len < NAMED_CHANNEL_NAME_MAX - 1u)
    {
        ch->name[name_len] = name_buf[name_len];
        ++name_len;
    }
    ch->name[name_len] = '\0';
    ch->owner_pid = get_current_pid(frame.runtime);
    ch->request_length = 0u;
    ch->request_sender_pid = 0u;
    ch->has_request = 0u;
    ch->reply_length = 0u;
    ch->has_reply = 0u;

    console_write("[IPC] channel created: ");
    console_write(ch->name);
    console_write(" slot=");
    console_write_dec((uint64_t)slot);
    console_write(" owner_pid=");
    console_write_dec((uint64_t)ch->owner_pid);
    console_write("\n");

    /* Return the slot index as the channel handle. */
    return zaclr_native_call_frame_set_i4(&frame, slot);
}

/* ---------------------------------------------------------------------- */
/* SysChannelOpen: open a named channel by service name.                   */
/* Args: (string name) -> int                                              */
/* ---------------------------------------------------------------------- */

struct zaclr_result zaclr_native_Zapada_System_Ipc_NativeCalls::SysChannelOpen___STATIC__I4__STRING(struct zaclr_native_call_frame& frame)
{
    ensure_initialized();

    if (frame.runtime == NULL)
        return zaclr_native_call_frame_set_i4(&frame, -1);

    const struct zaclr_string_desc* name_str;
    struct zaclr_result status = zaclr_native_call_frame_arg_string(&frame, 0u, &name_str);
    if (status.status != ZACLR_STATUS_OK || name_str == NULL)
        return zaclr_native_call_frame_set_i4(&frame, -2);

    static char name_buf[NAMED_CHANNEL_NAME_MAX];
    string_to_ascii(name_str, name_buf, NAMED_CHANNEL_NAME_MAX);

    if (name_buf[0] == '\0')
        return zaclr_native_call_frame_set_i4(&frame, -3);

    int32_t slot = find_by_name(name_buf);
    if (slot < 0)
    {
        console_write("[IPC] channel not found: ");
        console_write(name_buf);
        console_write("\n");
        return zaclr_native_call_frame_set_i4(&frame, -10);
    }

    return zaclr_native_call_frame_set_i4(&frame, slot);
}

/* ---------------------------------------------------------------------- */
/* SysChannelSend: send request bytes on a channel.                        */
/* Args: (int channel, byte[] data, int offset, int count) -> int          */
/* ---------------------------------------------------------------------- */

struct zaclr_result zaclr_native_Zapada_System_Ipc_NativeCalls::SysChannelSend___STATIC__I4__I4__SZARRAY_U1__I4__I4(struct zaclr_native_call_frame& frame)
{
    ensure_initialized();

    if (frame.runtime == NULL)
        return zaclr_native_call_frame_set_i4(&frame, -1);

    int32_t channel;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &channel);
    if (status.status != ZACLR_STATUS_OK)
        return zaclr_native_call_frame_set_i4(&frame, -2);

    const struct zaclr_array_desc* data_arr;
    status = zaclr_native_call_frame_arg_array(&frame, 1u, &data_arr);
    if (status.status != ZACLR_STATUS_OK || data_arr == NULL)
        return zaclr_native_call_frame_set_i4(&frame, -3);

    int32_t offset;
    status = zaclr_native_call_frame_arg_i4(&frame, 2u, &offset);
    if (status.status != ZACLR_STATUS_OK)
        return zaclr_native_call_frame_set_i4(&frame, -4);

    int32_t count;
    status = zaclr_native_call_frame_arg_i4(&frame, 3u, &count);
    if (status.status != ZACLR_STATUS_OK)
        return zaclr_native_call_frame_set_i4(&frame, -5);

    if (channel < 0 || (uint32_t)channel >= NAMED_CHANNEL_MAX)
        return zaclr_native_call_frame_set_i4(&frame, -10);

    struct named_channel_entry* ch = &s_named_channels[channel];
    if (ch->state != NCHAN_OPEN)
        return zaclr_native_call_frame_set_i4(&frame, -11);

    if (count < 0 || offset < 0)
        return zaclr_native_call_frame_set_i4(&frame, -12);

    uint32_t arr_len = zaclr_array_length(data_arr);
    if ((uint32_t)(offset + count) > arr_len)
        return zaclr_native_call_frame_set_i4(&frame, -13);

    if ((uint32_t)count > NAMED_CHANNEL_BUF_MAX)
        return zaclr_native_call_frame_set_i4(&frame, -14);

    /* Spin-wait if there is already a pending request (single-producer). */
    uint32_t spin = 0u;
    while (ch->has_request != 0u)
    {
        ++spin;
        if (spin > 100000u)
        {
            console_write("[IPC] send: spin timeout on channel ");
            console_write(ch->name);
            console_write("\n");
            return zaclr_native_call_frame_set_i4(&frame, -20);
        }
    }

    /* Copy request data from managed byte[] into channel buffer. */
    const uint8_t* src = (const uint8_t*)zaclr_array_data_const(data_arr) + offset;
    mem_copy(ch->request_buffer, src, (uint32_t)count);
    ch->request_length = (uint32_t)count;
    ch->request_sender_pid = get_current_pid(frame.runtime);

    /* Signal that a request is ready. */
    ch->has_request = 1u;

    /* Now spin-wait for the reply. */
    spin = 0u;
    while (ch->has_reply == 0u)
    {
        ++spin;
        if (spin > 1000000u)
        {
            console_write("[IPC] send: reply timeout on channel ");
            console_write(ch->name);
            console_write("\n");
            return zaclr_native_call_frame_set_i4(&frame, -21);
        }
    }

    /* Reply is ready. The caller reads it through SysChannelReceive. */
    /* For the synchronous request-reply pattern, we copy the reply back
       into the caller's data buffer now. */
    uint32_t reply_len = ch->reply_length;
    if (reply_len > (uint32_t)count) reply_len = (uint32_t)count;
    if (reply_len > arr_len) reply_len = arr_len;

    /* Write reply into the same data array (reusing the buffer). */
    uint8_t* dst = (uint8_t*)zaclr_array_data((struct zaclr_array_desc*)data_arr) + offset;
    mem_copy(dst, ch->reply_buffer, reply_len);

    /* Clear the reply flag. */
    ch->has_reply = 0u;

    /* Return bytes received in the reply. */
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)reply_len);
}

/* ---------------------------------------------------------------------- */
/* SysChannelReceive: receive request bytes (service side).                */
/* Args: (int channel, byte[] buffer, int offset, int capacity) -> int     */
/* ---------------------------------------------------------------------- */

struct zaclr_result zaclr_native_Zapada_System_Ipc_NativeCalls::SysChannelReceive___STATIC__I4__I4__SZARRAY_U1__I4__I4(struct zaclr_native_call_frame& frame)
{
    ensure_initialized();

    if (frame.runtime == NULL)
        return zaclr_native_call_frame_set_i4(&frame, -1);

    int32_t channel;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &channel);
    if (status.status != ZACLR_STATUS_OK)
        return zaclr_native_call_frame_set_i4(&frame, -2);

    const struct zaclr_array_desc* buf_arr;
    status = zaclr_native_call_frame_arg_array(&frame, 1u, &buf_arr);
    if (status.status != ZACLR_STATUS_OK || buf_arr == NULL)
        return zaclr_native_call_frame_set_i4(&frame, -3);

    int32_t offset;
    status = zaclr_native_call_frame_arg_i4(&frame, 2u, &offset);
    if (status.status != ZACLR_STATUS_OK)
        return zaclr_native_call_frame_set_i4(&frame, -4);

    int32_t capacity;
    status = zaclr_native_call_frame_arg_i4(&frame, 3u, &capacity);
    if (status.status != ZACLR_STATUS_OK)
        return zaclr_native_call_frame_set_i4(&frame, -5);

    if (channel < 0 || (uint32_t)channel >= NAMED_CHANNEL_MAX)
        return zaclr_native_call_frame_set_i4(&frame, -10);

    struct named_channel_entry* ch = &s_named_channels[channel];
    if (ch->state != NCHAN_OPEN)
        return zaclr_native_call_frame_set_i4(&frame, -11);

    if (capacity < 0 || offset < 0)
        return zaclr_native_call_frame_set_i4(&frame, -12);

    /* Spin-wait for a request to arrive. */
    uint32_t spin = 0u;
    while (ch->has_request == 0u)
    {
        ++spin;
        if (spin > 1000000u)
        {
            console_write("[IPC] recv: timeout on channel ");
            console_write(ch->name);
            console_write("\n");
            return zaclr_native_call_frame_set_i4(&frame, -20);
        }
    }

    /* Copy request data into managed buffer. */
    uint32_t copy_len = ch->request_length;
    if (copy_len > (uint32_t)capacity) copy_len = (uint32_t)capacity;

    uint32_t arr_len = zaclr_array_length(buf_arr);
    if ((uint32_t)(offset) + copy_len > arr_len)
        copy_len = arr_len - (uint32_t)offset;

    uint8_t* dst = (uint8_t*)zaclr_array_data((struct zaclr_array_desc*)buf_arr) + offset;
    mem_copy(dst, ch->request_buffer, copy_len);

    /* Return bytes received. */
    return zaclr_native_call_frame_set_i4(&frame, (int32_t)copy_len);
}

/* ---------------------------------------------------------------------- */
/* SysChannelReply: send reply bytes on a service channel.                 */
/* Args: (int channel, byte[] data, int offset, int count) -> int          */
/* ---------------------------------------------------------------------- */

struct zaclr_result zaclr_native_Zapada_System_Ipc_NativeCalls::SysChannelReply___STATIC__I4__I4__SZARRAY_U1__I4__I4(struct zaclr_native_call_frame& frame)
{
    ensure_initialized();

    if (frame.runtime == NULL)
        return zaclr_native_call_frame_set_i4(&frame, -1);

    int32_t channel;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &channel);
    if (status.status != ZACLR_STATUS_OK)
        return zaclr_native_call_frame_set_i4(&frame, -2);

    const struct zaclr_array_desc* data_arr;
    status = zaclr_native_call_frame_arg_array(&frame, 1u, &data_arr);
    if (status.status != ZACLR_STATUS_OK || data_arr == NULL)
        return zaclr_native_call_frame_set_i4(&frame, -3);

    int32_t offset;
    status = zaclr_native_call_frame_arg_i4(&frame, 2u, &offset);
    if (status.status != ZACLR_STATUS_OK)
        return zaclr_native_call_frame_set_i4(&frame, -4);

    int32_t count;
    status = zaclr_native_call_frame_arg_i4(&frame, 3u, &count);
    if (status.status != ZACLR_STATUS_OK)
        return zaclr_native_call_frame_set_i4(&frame, -5);

    if (channel < 0 || (uint32_t)channel >= NAMED_CHANNEL_MAX)
        return zaclr_native_call_frame_set_i4(&frame, -10);

    struct named_channel_entry* ch = &s_named_channels[channel];
    if (ch->state != NCHAN_OPEN)
        return zaclr_native_call_frame_set_i4(&frame, -11);

    if (count < 0 || offset < 0)
        return zaclr_native_call_frame_set_i4(&frame, -12);

    uint32_t arr_len = zaclr_array_length(data_arr);
    if ((uint32_t)(offset + count) > arr_len)
        return zaclr_native_call_frame_set_i4(&frame, -13);

    if ((uint32_t)count > NAMED_CHANNEL_BUF_MAX)
        return zaclr_native_call_frame_set_i4(&frame, -14);

    /* Copy reply data from managed byte[] into channel buffer. */
    const uint8_t* src = (const uint8_t*)zaclr_array_data_const(data_arr) + offset;
    mem_copy(ch->reply_buffer, src, (uint32_t)count);
    ch->reply_length = (uint32_t)count;

    /* Clear the request flag and signal reply ready. */
    ch->has_request = 0u;
    ch->has_reply = 1u;

    return zaclr_native_call_frame_set_i4(&frame, 0);
}

/* ---------------------------------------------------------------------- */
/* SysChannelClose: close a channel handle.                                */
/* Args: (int channel) -> int                                              */
/* ---------------------------------------------------------------------- */

struct zaclr_result zaclr_native_Zapada_System_Ipc_NativeCalls::SysChannelClose___STATIC__I4__I4(struct zaclr_native_call_frame& frame)
{
    ensure_initialized();

    if (frame.runtime == NULL)
        return zaclr_native_call_frame_set_i4(&frame, -1);

    int32_t channel;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &channel);
    if (status.status != ZACLR_STATUS_OK)
        return zaclr_native_call_frame_set_i4(&frame, -2);

    if (channel < 0 || (uint32_t)channel >= NAMED_CHANNEL_MAX)
        return zaclr_native_call_frame_set_i4(&frame, -10);

    struct named_channel_entry* ch = &s_named_channels[channel];
    if (ch->state != NCHAN_OPEN)
        return zaclr_native_call_frame_set_i4(&frame, -11);

    console_write("[IPC] channel closed: ");
    console_write(ch->name);
    console_write("\n");

    ch->state = NCHAN_CLOSED;
    ch->name[0] = '\0';
    ch->owner_pid = 0u;
    ch->request_length = 0u;
    ch->has_request = 0u;
    ch->reply_length = 0u;
    ch->has_reply = 0u;

    /* Mark as free for reuse. */
    ch->state = NCHAN_FREE;

    return zaclr_native_call_frame_set_i4(&frame, 0);
}

/* ---------------------------------------------------------------------- */
/* SysChannelGetSenderPid: get the sender of the last request.             */
/* Args: (int channel) -> int                                              */
/* ---------------------------------------------------------------------- */

struct zaclr_result zaclr_native_Zapada_System_Ipc_NativeCalls::SysChannelGetSenderPid___STATIC__I4__I4(struct zaclr_native_call_frame& frame)
{
    ensure_initialized();

    if (frame.runtime == NULL)
        return zaclr_native_call_frame_set_i4(&frame, -1);

    int32_t channel;
    struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &channel);
    if (status.status != ZACLR_STATUS_OK)
        return zaclr_native_call_frame_set_i4(&frame, -2);

    if (channel < 0 || (uint32_t)channel >= NAMED_CHANNEL_MAX)
        return zaclr_native_call_frame_set_i4(&frame, -10);

    struct named_channel_entry* ch = &s_named_channels[channel];
    if (ch->state != NCHAN_OPEN)
        return zaclr_native_call_frame_set_i4(&frame, -11);

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)ch->request_sender_pid);
}
