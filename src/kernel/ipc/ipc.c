/*
 * Zapada - src/kernel/ipc/ipc.c
 *
 * IPC v1 bounded typed message channel implementation for Phase 2B/2C.
 *
 * Ring buffer implementation:
 *   - Each channel has a fixed-size circular buffer of IPC_CHANNEL_CAPACITY
 *     ipc_message_t slots.
 *   - head: index of the oldest message (next to dequeue).
 *   - tail: index of the next write slot.
 *   - count: number of valid messages. Used for full/empty checks.
 *   - count == 0 => empty; count == IPC_CHANNEL_CAPACITY => full.
 *
 * Phase 2C additions:
 *   - blocked_sender: pointer to the one thread blocked waiting for space.
 *   - blocked_receiver: pointer to the one thread blocked waiting for a message.
 *   - ipc_trysend wakes blocked_receiver on successful enqueue.
 *   - ipc_tryrecv wakes blocked_sender on successful dequeue.
 *   - ipc_send / ipc_recv loop: block calling thread on full/empty,
 *     context-switch to next READY thread, retry on wakeup.
 *
 * No heap dependency. All state is in the static channel pool.
 */

#include <kernel/ipc/ipc.h>
#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <kernel/console.h>
#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Channel state                                                           */
/* ---------------------------------------------------------------------- */

typedef enum {
    CHAN_FREE   = 0,
    CHAN_OPEN   = 1,
    CHAN_CLOSED = 2,
} channel_state_t;

typedef struct {
    channel_state_t state;
    ipc_handle_t    handle;
    uint32_t        head;
    uint32_t        tail;
    uint32_t        count;
    ipc_message_t   ring[IPC_CHANNEL_CAPACITY];

    /*
     * Phase 2C: at most one thread may block on send (channel full) and
     * at most one thread may block on receive (channel empty) at a time.
     * NULL when no thread is waiting.
     */
    thread_t       *blocked_sender;
    thread_t       *blocked_receiver;
} ipc_channel_t;

static ipc_channel_t s_channels[IPC_MAX_CHANNELS];
static uint32_t      s_next_generation;

/* ---------------------------------------------------------------------- */
/* Internal helpers                                                        */
/* ---------------------------------------------------------------------- */

static ipc_channel_t *chan_from_handle(ipc_handle_t h)
{
    uint32_t idx;
    ipc_channel_t *ch;

    if (h == IPC_HANDLE_INVALID) {
        return NULL;
    }

    idx = (h & 0xFFu);
    if (idx == 0u || idx > IPC_MAX_CHANNELS) {
        return NULL;
    }

    ch = &s_channels[idx - 1u];
    if (ch->handle != h) {
        return NULL;
    }

    return ch;
}

static void msg_copy(ipc_message_t *dst, const ipc_message_t *src)
{
    uint32_t i;
    dst->type        = src->type;
    dst->payload_len = src->payload_len;
    for (i = 0; i < (IPC_MSG_PAYLOAD_MAX / 8u); i++) {
        dst->payload[i] = src->payload[i];
    }
}

/* ---------------------------------------------------------------------- */
/* ipc_result_name                                                         */
/* ---------------------------------------------------------------------- */

const char *ipc_result_name(ipc_result_t r)
{
    switch (r) {
        case IPC_OK:         return "IPC_OK";
        case IPC_ERR_INVAL:  return "IPC_ERR_INVAL";
        case IPC_ERR_FULL:   return "IPC_ERR_FULL";
        case IPC_ERR_EMPTY:  return "IPC_ERR_EMPTY";
        case IPC_ERR_AGAIN:  return "IPC_ERR_AGAIN";
        case IPC_ERR_TYPE:   return "IPC_ERR_TYPE";
        case IPC_ERR_CLOSED: return "IPC_ERR_CLOSED";
        case IPC_ERR_NOSLOT: return "IPC_ERR_NOSLOT";
        default:             return "IPC_ERR_UNKNOWN";
    }
}

/* ---------------------------------------------------------------------- */
/* ipc_init                                                                */
/* ---------------------------------------------------------------------- */

void ipc_init(void)
{
    uint32_t i;

    for (i = 0; i < IPC_MAX_CHANNELS; i++) {
        s_channels[i].state            = CHAN_FREE;
        s_channels[i].handle           = IPC_HANDLE_INVALID;
        s_channels[i].head             = 0;
        s_channels[i].tail             = 0;
        s_channels[i].count            = 0;
        s_channels[i].blocked_sender   = NULL;
        s_channels[i].blocked_receiver = NULL;
    }

    s_next_generation = 1u;

    console_write("IPC             : initialized (");
    console_write_dec((uint64_t)IPC_MAX_CHANNELS);
    console_write(" channels, capacity=");
    console_write_dec((uint64_t)IPC_CHANNEL_CAPACITY);
    console_write(", payload_max=");
    console_write_dec((uint64_t)IPC_MSG_PAYLOAD_MAX);
    console_write(")\n");
}

/* ---------------------------------------------------------------------- */
/* ipc_channel_create                                                      */
/* ---------------------------------------------------------------------- */

ipc_handle_t ipc_channel_create(void)
{
    uint32_t      i;
    uint32_t      generation;
    ipc_handle_t  h;

    /* Find a free slot */
    for (i = 0; i < IPC_MAX_CHANNELS; i++) {
        if (s_channels[i].state == CHAN_FREE) {
            generation = s_next_generation++;
            if (s_next_generation == 0u) {
                s_next_generation = 1u;
            }

            h = (ipc_handle_t)(((generation & 0x00FFFFFFu) << 8u) | (i + 1u));
            if (h == IPC_HANDLE_INVALID) {
                h = (ipc_handle_t)(i + 1u);
            }

            s_channels[i].state            = CHAN_OPEN;
            s_channels[i].handle           = h;
            s_channels[i].head             = 0;
            s_channels[i].tail             = 0;
            s_channels[i].count            = 0;
            s_channels[i].blocked_sender   = NULL;
            s_channels[i].blocked_receiver = NULL;

            return h;
        }
    }

    console_write("IPC             : ERROR: channel pool exhausted\n");
    return IPC_HANDLE_INVALID;
}

/* ---------------------------------------------------------------------- */
/* ipc_channel_destroy                                                     */
/* ---------------------------------------------------------------------- */

ipc_result_t ipc_channel_destroy(ipc_handle_t h)
{
    ipc_channel_t *ch;

    if (h == IPC_HANDLE_INVALID) {
        console_write("IPC             : destroy ERROR: invalid handle\n");
        return IPC_ERR_INVAL;
    }

    ch = chan_from_handle(h);
    if (ch == NULL) {
        return IPC_ERR_INVAL;
    }
    if (ch->state == CHAN_FREE) {
        return IPC_ERR_INVAL;
    }

    ch->state            = CHAN_FREE;
    ch->handle           = IPC_HANDLE_INVALID;
    ch->head             = 0;
    ch->tail             = 0;
    ch->count            = 0;
    ch->blocked_sender   = NULL;
    ch->blocked_receiver = NULL;

    return IPC_OK;
}

/* ---------------------------------------------------------------------- */
/* ipc_channel_is_open                                                     */
/* ---------------------------------------------------------------------- */

bool ipc_channel_is_open(ipc_handle_t h)
{
    ipc_channel_t *ch;

    if (h == IPC_HANDLE_INVALID) {
        return false;
    }

    ch = chan_from_handle(h);
    return ch != NULL && ch->state == CHAN_OPEN;
}

/* ---------------------------------------------------------------------- */
/* ipc_trysend                                                             */
/* ---------------------------------------------------------------------- */

ipc_result_t ipc_trysend(ipc_handle_t h, const ipc_message_t *msg)
{
    ipc_channel_t *ch;

    if (h == IPC_HANDLE_INVALID || msg == NULL) {
        return IPC_ERR_INVAL;
    }

    ch = chan_from_handle(h);
    if (ch == NULL) {
        return IPC_ERR_INVAL;
    }
    if (ch->state == CHAN_FREE) {
        return IPC_ERR_INVAL;
    }
    if (ch->state == CHAN_CLOSED) {
        return IPC_ERR_CLOSED;
    }
    if (ch->count >= IPC_CHANNEL_CAPACITY) {
        return IPC_ERR_FULL;
    }

    /* Copy message into the tail slot */
    msg_copy(&ch->ring[ch->tail], msg);
    ch->tail = (ch->tail + 1u) % IPC_CHANNEL_CAPACITY;
    ch->count++;

    /*
     * Phase 2C: wake any thread that was blocked waiting for a message.
     * Clear the pointer before calling sched_unblock so the unblocked
     * thread does not see a stale pointer when it retries ipc_tryrecv.
     */
    if (ch->blocked_receiver != NULL) {
        thread_t *waiter = ch->blocked_receiver;
        ch->blocked_receiver = NULL;
        (void)sched_unblock(waiter);
    }

    return IPC_OK;
}

/* ---------------------------------------------------------------------- */
/* ipc_tryrecv                                                             */
/* ---------------------------------------------------------------------- */

ipc_result_t ipc_tryrecv(ipc_handle_t h, uint32_t type_filter,
                          ipc_message_t *msg_out)
{
    ipc_channel_t      *ch;
    const ipc_message_t *head_msg;

    if (h == IPC_HANDLE_INVALID || msg_out == NULL) {
        return IPC_ERR_INVAL;
    }

    ch = chan_from_handle(h);
    if (ch == NULL) {
        return IPC_ERR_INVAL;
    }
    if (ch->state == CHAN_FREE) {
        return IPC_ERR_INVAL;
    }
    if (ch->state == CHAN_CLOSED) {
        return IPC_ERR_CLOSED;
    }
    if (ch->count == 0u) {
        return IPC_ERR_EMPTY;
    }

    head_msg = &ch->ring[ch->head];

    /* Type filter check (IPC_MSG_TYPE_ANY accepts everything) */
    if (type_filter != IPC_MSG_TYPE_ANY && head_msg->type != type_filter) {
        return IPC_ERR_TYPE;
    }

    /* Dequeue */
    msg_copy(msg_out, head_msg);
    ch->head = (ch->head + 1u) % IPC_CHANNEL_CAPACITY;
    ch->count--;

    /*
     * Phase 2C: wake any thread that was blocked waiting for buffer space.
     */
    if (ch->blocked_sender != NULL) {
        thread_t *waiter = ch->blocked_sender;
        ch->blocked_sender = NULL;
        (void)sched_unblock(waiter);
    }

    return IPC_OK;
}

/* ---------------------------------------------------------------------- */
/* ipc_send - blocking send (Phase 2C)                                    */
/* ---------------------------------------------------------------------- */

ipc_result_t ipc_send(ipc_handle_t h, const ipc_message_t *msg)
{
    ipc_channel_t *ch;
    ipc_result_t   r;
    thread_t      *current;
    thread_t      *next;

    if (h == IPC_HANDLE_INVALID || msg == NULL) {
        return IPC_ERR_INVAL;
    }

    ch = chan_from_handle(h);
    if (ch == NULL) {
        return IPC_ERR_INVAL;
    }
    if (ch->state == CHAN_FREE) {
        return IPC_ERR_INVAL;
    }
    if (ch->state == CHAN_CLOSED) {
        return IPC_ERR_CLOSED;
    }

    for (;;) {
        r = ipc_trysend(h, msg);

        if (r != IPC_ERR_FULL) {
            /* IPC_OK or a non-retriable error */
            return r;
        }

        /* Channel is full. Block the calling thread if a scheduler context
         * is available; otherwise return IPC_ERR_AGAIN. */
        current = sched_get_current();
        if (current == NULL) {
            return IPC_ERR_AGAIN;
        }

        console_write("ipc_send        : channel full, blocking tid=");
        console_write_dec((uint64_t)current->tid);
        console_write("\n");

        ch->blocked_sender = current;

        if (!sched_block(current)) {
            /* sched_block failed: clear pointer and give up */
            ch->blocked_sender = NULL;
            return IPC_ERR_AGAIN;
        }

        /* Pick the next READY thread to run */
        next = sched_dequeue();
        if (next == NULL) {
            /* No runnable thread: cannot block (would deadlock). Undo block. */
            ch->blocked_sender = NULL;
            current->state = PROCESS_STATE_READY;
            (void)sched_enqueue(current);
            current->state = PROCESS_STATE_RUNNING;
            sched_set_current(current);
            return IPC_ERR_AGAIN;
        }

        /* Switch to next; when we return here we have been unblocked. */
        sched_context_switch(current, next);
        /* On return: current->state = RUNNING (set by sched_context_switch),
         * s_current = current. Loop and retry. */
    }
}

/* ---------------------------------------------------------------------- */
/* ipc_recv - blocking receive (Phase 2C)                                 */
/* ---------------------------------------------------------------------- */

ipc_result_t ipc_recv(ipc_handle_t h, uint32_t type_filter,
                       ipc_message_t *msg_out)
{
    ipc_channel_t *ch;
    ipc_result_t   r;
    thread_t      *current;
    thread_t      *next;

    if (h == IPC_HANDLE_INVALID || msg_out == NULL) {
        return IPC_ERR_INVAL;
    }

    ch = chan_from_handle(h);
    if (ch == NULL) {
        return IPC_ERR_INVAL;
    }
    if (ch->state == CHAN_FREE) {
        return IPC_ERR_INVAL;
    }
    if (ch->state == CHAN_CLOSED) {
        return IPC_ERR_CLOSED;
    }

    for (;;) {
        r = ipc_tryrecv(h, type_filter, msg_out);

        if (r != IPC_ERR_EMPTY) {
            /* IPC_OK, IPC_ERR_TYPE, or a non-retriable error */
            return r;
        }

        /* Channel is empty. Block the calling thread if a scheduler context
         * is available; otherwise return IPC_ERR_AGAIN. */
        current = sched_get_current();
        if (current == NULL) {
            return IPC_ERR_AGAIN;
        }

        console_write("ipc_recv        : channel empty, blocking tid=");
        console_write_dec((uint64_t)current->tid);
        console_write("\n");

        ch->blocked_receiver = current;

        if (!sched_block(current)) {
            ch->blocked_receiver = NULL;
            return IPC_ERR_AGAIN;
        }

        next = sched_dequeue();
        if (next == NULL) {
            ch->blocked_receiver = NULL;
            current->state = PROCESS_STATE_READY;
            (void)sched_enqueue(current);
            current->state = PROCESS_STATE_RUNNING;
            sched_set_current(current);
            return IPC_ERR_AGAIN;
        }

        sched_context_switch(current, next);
        /* On return: unblocked and rescheduled. Retry receive. */
    }
}

/* ---------------------------------------------------------------------- */
/* ipc_channel_len                                                         */
/* ---------------------------------------------------------------------- */

uint32_t ipc_channel_len(ipc_handle_t h)
{
    ipc_channel_t *ch;

    if (h == IPC_HANDLE_INVALID) {
        return 0;
    }

    ch = chan_from_handle(h);
    if (ch == NULL || ch->state != CHAN_OPEN) {
        return 0;
    }

    return ch->count;
}

