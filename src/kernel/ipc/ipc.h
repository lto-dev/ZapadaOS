/*
 * Zapada - src/kernel/ipc/ipc.h
 *
 * IPC v1: bounded typed message channel primitives for Phase 2B/2C.
 *
 * Design:
 *   - Typed messages: each message carries a 32-bit type tag and up to
 *     IPC_MSG_PAYLOAD_MAX bytes of payload.
 *   - Bounded channels: each channel has a fixed-capacity FIFO ring buffer
 *     (IPC_CHANNEL_CAPACITY messages). Full/empty conditions are explicit
 *     errors on the non-blocking path.
 *   - Explicit failure semantics:
 *       - Send to a full channel: IPC_ERR_FULL (trysend) / blocks (send)
 *       - Receive from an empty channel: IPC_ERR_EMPTY (tryrecv) / blocks (recv)
 *       - Invalid channel handle: IPC_ERR_INVAL
 *       - Type mismatch on receive: IPC_ERR_TYPE
 *   - Static channel pool: IPC_MAX_CHANNELS pre-allocated channels. No
 *     heap dependency for channel metadata.
 *   - AArch64 alignment safety: all payload storage is uint64_t-aligned.
 *
 * Phase 2C additions:
 *   - ipc_send / ipc_recv now block the calling thread when the channel is
 *     full/empty. One blocked-sender and one blocked-receiver slot per channel.
 *     Blocking requires scheduler integration (sched_block / sched_unblock /
 *     sched_context_switch). If no scheduler context is available (current
 *     thread is NULL), falls back to IPC_ERR_AGAIN.
 */

#ifndef ZAPADA_IPC_H
#define ZAPADA_IPC_H

#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* IPC constants                                                           */
/* ---------------------------------------------------------------------- */

/*
 * IPC_CHANNEL_CAPACITY - number of messages a channel can buffer before
 * ipc_trysend returns IPC_ERR_FULL.
 */
#define IPC_CHANNEL_CAPACITY    8u

/*
 * IPC_MSG_PAYLOAD_MAX - maximum payload bytes per message.
 * Must be a multiple of 8 for AArch64 alignment safety.
 */
#define IPC_MSG_PAYLOAD_MAX     64u

/*
 * IPC_MAX_CHANNELS - maximum simultaneously open channels in the static pool.
 */
#define IPC_MAX_CHANNELS        16u

/* ---------------------------------------------------------------------- */
/* IPC result codes                                                        */
/* ---------------------------------------------------------------------- */

typedef int32_t ipc_result_t;

#define IPC_OK              ((ipc_result_t)  0)
#define IPC_ERR_INVAL       ((ipc_result_t) -1)   /* Invalid argument or handle */
#define IPC_ERR_FULL        ((ipc_result_t) -2)   /* Channel buffer full */
#define IPC_ERR_EMPTY       ((ipc_result_t) -3)   /* Channel buffer empty */
#define IPC_ERR_AGAIN       ((ipc_result_t) -4)   /* Would block (stub) */
#define IPC_ERR_TYPE        ((ipc_result_t) -5)   /* Message type mismatch */
#define IPC_ERR_CLOSED      ((ipc_result_t) -6)   /* Channel is closed/destroyed */
#define IPC_ERR_NOSLOT      ((ipc_result_t) -7)   /* Channel pool exhausted */

/* ---------------------------------------------------------------------- */
/* IPC message type                                                        */
/* ---------------------------------------------------------------------- */

/*
 * Well-known message type tags. Values above IPC_MSG_TYPE_USER are free
 * for managed-layer use.
 */
#define IPC_MSG_TYPE_ANY        0x00000000u  /* Wildcard: accept any type */
#define IPC_MSG_TYPE_SIGNAL     0x00000001u  /* Simple signal, no payload */
#define IPC_MSG_TYPE_DATA       0x00000002u  /* Raw data payload */
#define IPC_MSG_TYPE_MANAGED    0x00000010u  /* Managed object reference */
#define IPC_MSG_TYPE_USER       0x00010000u  /* Base for user-defined types */

/* ---------------------------------------------------------------------- */
/* IPC message structure                                                   */
/* ---------------------------------------------------------------------- */

/*
 * ipc_message_t - one message in the channel FIFO.
 *
 * AArch64 alignment: all fields are naturally aligned. payload is declared
 * as uint64_t array to guarantee 8-byte alignment of the first payload byte.
 */
typedef struct {
    uint32_t type;                              /* Message type tag */
    uint32_t payload_len;                       /* Bytes of valid payload data */
    uint64_t payload[IPC_MSG_PAYLOAD_MAX / 8u]; /* 8-byte aligned payload */
} ipc_message_t;

/* ---------------------------------------------------------------------- */
/* Channel handle type                                                     */
/* ---------------------------------------------------------------------- */

typedef uint32_t ipc_handle_t;

#define IPC_HANDLE_INVALID  ((ipc_handle_t)0u)

/* ---------------------------------------------------------------------- */
/* IPC API                                                                 */
/* ---------------------------------------------------------------------- */

/*
 * ipc_init - initialize the IPC channel pool.
 *
 * Must be called during kernel initialization, before any channel is created.
 */
void ipc_init(void);

/*
 * ipc_channel_create - allocate and open a new channel.
 *
 * Returns a valid handle on success, IPC_HANDLE_INVALID if the pool is
 * exhausted.
 */
ipc_handle_t ipc_channel_create(void);

/*
 * ipc_channel_destroy - release the channel and invalidate its handle.
 *
 * Any pending messages in the buffer are discarded.
 * Returns IPC_OK on success, IPC_ERR_INVAL if the handle is invalid.
 */
ipc_result_t ipc_channel_destroy(ipc_handle_t h);

/*
 * ipc_channel_is_open - validate that a channel handle currently names an
 * open channel without mutating channel state.
 */
bool ipc_channel_is_open(ipc_handle_t h);

/*
 * ipc_trysend - non-blocking: copy msg into the channel ring buffer.
 *
 * Returns:
 *   IPC_OK        - message enqueued.
 *   IPC_ERR_FULL  - channel buffer is full.
 *   IPC_ERR_INVAL - h is invalid or msg is NULL.
 *   IPC_ERR_CLOSED - channel has been destroyed.
 */
ipc_result_t ipc_trysend(ipc_handle_t h, const ipc_message_t *msg);

/*
 * ipc_tryrecv - non-blocking: copy the head message out of the ring buffer.
 *
 * If type_filter != IPC_MSG_TYPE_ANY and the head message's type does not
 * match, returns IPC_ERR_TYPE and does NOT dequeue the message.
 *
 * Returns:
 *   IPC_OK        - message dequeued and copied to *msg_out.
 *   IPC_ERR_EMPTY - no message in the buffer.
 *   IPC_ERR_TYPE  - head message type does not match type_filter.
 *   IPC_ERR_INVAL - h is invalid or msg_out is NULL.
 *   IPC_ERR_CLOSED - channel has been destroyed.
 */
ipc_result_t ipc_tryrecv(ipc_handle_t h, uint32_t type_filter,
                          ipc_message_t *msg_out);

/*
 * ipc_send - blocking send (Phase 2C: real scheduler-integrated blocking).
 *
 * Attempts ipc_trysend. If the channel is full and a current thread exists,
 * blocks the calling thread (via sched_block + sched_context_switch) and
 * retries on wakeup. Returns IPC_ERR_AGAIN only if no scheduler context
 * is available. Returns IPC_OK when the message was successfully enqueued.
 */
ipc_result_t ipc_send(ipc_handle_t h, const ipc_message_t *msg);

/*
 * ipc_recv - blocking receive (Phase 2C: real scheduler-integrated blocking).
 *
 * Attempts ipc_tryrecv. If the channel is empty and a current thread exists,
 * blocks the calling thread and retries on wakeup. Returns IPC_ERR_AGAIN only
 * if no scheduler context is available.
 */
ipc_result_t ipc_recv(ipc_handle_t h, uint32_t type_filter,
                       ipc_message_t *msg_out);

/*
 * ipc_channel_len - return the number of messages currently in the buffer.
 *
 * Returns 0 if h is invalid.
 */
uint32_t ipc_channel_len(ipc_handle_t h);

/*
 * ipc_result_name - return a human-readable string for an ipc_result_t.
 */
const char *ipc_result_name(ipc_result_t r);

#endif /* ZAPADA_IPC_H */


