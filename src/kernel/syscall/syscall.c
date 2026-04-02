/*
 * Zapada - src/kernel/syscall/syscall.c
 *
 * Syscall dispatch implementation for Phase 2B/2C.
 *
 * Each syscall handler is a static function with the signature:
 *   syscall_result_t handle_XXX(const syscall_args_t *args)
 *
 * The dispatcher table maps syscall numbers to handlers. Unknown numbers
 * return SYSCALL_ERR_NOSYS with a diagnostic message.
 *
 * Phase 2C additions:
 *   - handle_yield: uses real sched_get_current + sched_context_switch if
 *     a current thread is tracked.
 *   - handle_sched_set_priority: changes current thread's priority.
 *   - handle_sched_sleep: cooperative yield (blocks for a scheduler round).
 *   - handle_sched_get_priority: returns current thread's priority.
 */

#include <kernel/syscall/syscall.h>
#include <kernel/ipc/ipc.h>
#include <kernel/sched/sched.h>
#include <kernel/console.h>
#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Internal: print a syscall call record                                  */
/* ---------------------------------------------------------------------- */

static void syscall_trace_entry(uint64_t number)
{
    console_write("syscall         : nr=0x");
    console_write_hex64(number);
    console_write("\n");
}

static void syscall_trace_result(syscall_result_t result)
{
    if (result < 0) {
        console_write("syscall         : result=ERR(");
        /* Print as signed */
        int64_t r = (int64_t)result;
        if (r < 0) {
            console_write("-");
            console_write_dec((uint64_t)(-r));
        } else {
            console_write_dec((uint64_t)r);
        }
        console_write(")\n");
    } else {
        console_write("syscall         : result=");
        console_write_dec((uint64_t)result);
        console_write("\n");
    }
}

static syscall_result_t ipc_result_to_syscall_result(ipc_result_t r)
{
    switch (r) {
        case IPC_OK:         return SYSCALL_OK;
        case IPC_ERR_INVAL:  return SYSCALL_ERR_INVAL;
        case IPC_ERR_FULL:   return SYSCALL_ERR_FULL;
        case IPC_ERR_EMPTY:  return SYSCALL_ERR_EMPTY;
        case IPC_ERR_AGAIN:  return SYSCALL_ERR_AGAIN;
        case IPC_ERR_TYPE:   return SYSCALL_ERR_INVAL;
        case IPC_ERR_CLOSED: return SYSCALL_ERR_INVAL;
        case IPC_ERR_NOSLOT: return SYSCALL_ERR_AGAIN;
        default:             return SYSCALL_ERR_UNKNOWN;
    }
}

/* ---------------------------------------------------------------------- */
/* Handlers - SYSCALL_YIELD                                               */
/* ---------------------------------------------------------------------- */

static syscall_result_t handle_yield(const syscall_args_t *args)
{
    thread_t *current;
    thread_t *next;

    (void)args;

    current = sched_get_current();
    if (current == NULL) {
        console_write("syscall         : YIELD (no current thread, no-op)\n");
        return SYSCALL_OK;
    }

    /*
     * Phase 2C: perform a real cooperative yield.
     * 1. Move current from RUNNING -> READY and re-queue it.
     * 2. Pick the next highest-priority READY thread.
     * 3. Context-switch to it (or remain on current if queue is empty).
     */
    if (!sched_yield(current)) {
        /* sched_yield failed (current not RUNNING): no-op */
        return SYSCALL_OK;
    }

    next = sched_dequeue();
    if (next == NULL) {
        /* No other thread available: re-run current */
        current->state = PROCESS_STATE_RUNNING;
        sched_set_current(current);
        return SYSCALL_OK;
    }

    /* current is already READY and enqueued; switch to next */
    sched_context_switch(current, next);
    return SYSCALL_OK;
}

/* ---------------------------------------------------------------------- */
/* Handlers - SYSCALL_EXIT                                                */
/* ---------------------------------------------------------------------- */

static syscall_result_t handle_exit(const syscall_args_t *args)
{
    int32_t code = (int32_t)(args->arg0 & 0xFFFFFFFFu);
    console_write("syscall         : EXIT code=");
    console_write_dec((uint64_t)(uint32_t)code);
    console_write(" (stub)\n");
    return SYSCALL_OK;
}

/* ---------------------------------------------------------------------- */
/* Handlers - SYSCALL_GET_PID / SYSCALL_GET_TID                          */
/* ---------------------------------------------------------------------- */

static syscall_result_t handle_get_pid(const syscall_args_t *args)
{
    (void)args;
    /*
     * Phase 2B: return kernel process PID. The current process table is not
     * yet fully wired to syscall context in 2B.
     */
    return (syscall_result_t)PID_KERNEL;
}

static syscall_result_t handle_get_tid(const syscall_args_t *args)
{
    (void)args;
    return (syscall_result_t)1;  /* Phase 2B: stub returns tid 1 */
}

/* ---------------------------------------------------------------------- */
/* Handlers - SYSCALL_SCHED_BLOCK                                        */
/* ---------------------------------------------------------------------- */

static syscall_result_t handle_sched_block(const syscall_args_t *args)
{
    thread_t *current;
    thread_t *next;

    (void)args;

    current = sched_get_current();
    if (current == NULL) {
        console_write("syscall         : SCHED_BLOCK (no current thread, no-op)\n");
        return SYSCALL_OK;
    }

    if (!sched_block(current)) {
        return SYSCALL_ERR_INVAL;
    }

    next = sched_dequeue();
    if (next == NULL) {
        /* No other thread: undo block */
        current->state = PROCESS_STATE_READY;
        (void)sched_enqueue(current);
        current->state = PROCESS_STATE_RUNNING;
        sched_set_current(current);
        return SYSCALL_ERR_AGAIN;
    }

    sched_context_switch(current, next);
    return SYSCALL_OK;
}

/* ---------------------------------------------------------------------- */
/* Handlers - Phase 2C scheduler policy hooks                             */
/* ---------------------------------------------------------------------- */

static syscall_result_t handle_sched_set_priority(const syscall_args_t *args)
{
    thread_t *current;
    uint32_t  new_prio;

    new_prio = (uint32_t)(args->arg0 & 0xFFFFFFFFu);
    if (new_prio >= SCHED_PRIO_MAX) {
        console_write("syscall         : SCHED_SET_PRIORITY: priority out of range\n");
        return SYSCALL_ERR_INVAL;
    }

    current = sched_get_current();
    if (current == NULL) {
        console_write("syscall         : SCHED_SET_PRIORITY: no current thread\n");
        return SYSCALL_ERR_AGAIN;
    }

    console_write("syscall         : SCHED_SET_PRIORITY tid=");
    console_write_dec((uint64_t)current->tid);
    console_write(" prio=");
    console_write_dec((uint64_t)new_prio);
    console_write("\n");

    current->priority = new_prio;
    return SYSCALL_OK;
}

static syscall_result_t handle_sched_sleep(const syscall_args_t *args)
{
    thread_t *current;
    thread_t *next;

    /*
     * Phase 2C cooperative sleep: yield the thread for one scheduler round.
     * arg0 is the requested sleep duration in ticks; for Phase 2C we do a
     * single yield rather than blocking for the full duration (no timer
     * wakeup list is implemented yet). The value is logged for diagnostic
     * purposes.
     */
    console_write("syscall         : SCHED_SLEEP ticks=");
    console_write_dec(args->arg0);
    console_write(" (cooperative yield)\n");

    current = sched_get_current();
    if (current == NULL) {
        return SYSCALL_OK;
    }

    if (!sched_yield(current)) {
        return SYSCALL_OK;
    }

    next = sched_dequeue();
    if (next == NULL) {
        current->state = PROCESS_STATE_RUNNING;
        sched_set_current(current);
        return SYSCALL_OK;
    }

    sched_context_switch(current, next);
    return SYSCALL_OK;
}

static syscall_result_t handle_sched_get_priority(const syscall_args_t *args)
{
    thread_t *current;

    (void)args;
    current = sched_get_current();
    if (current == NULL) {
        return SYSCALL_ERR_AGAIN;
    }

    return (syscall_result_t)current->priority;
}

/* ---------------------------------------------------------------------- */
/* Handlers - IPC stubs (forward to ipc layer via 2B stub)               */
/* ---------------------------------------------------------------------- */

static syscall_result_t handle_ipc_send(const syscall_args_t *args)
{
    ipc_handle_t          h;
    const ipc_message_t  *msg;

    h   = (ipc_handle_t)(args->arg0 & 0xFFFFFFFFu);
    msg = (const ipc_message_t *)(uintptr_t)args->arg1;

    return ipc_result_to_syscall_result(ipc_send(h, msg));
}

static syscall_result_t handle_ipc_recv(const syscall_args_t *args)
{
    ipc_handle_t    h;
    uint32_t        type_filter;
    ipc_message_t  *msg_out;

    h           = (ipc_handle_t)(args->arg0 & 0xFFFFFFFFu);
    type_filter = (uint32_t)(args->arg1 & 0xFFFFFFFFu);
    msg_out     = (ipc_message_t *)(uintptr_t)args->arg2;

    return ipc_result_to_syscall_result(ipc_recv(h, type_filter, msg_out));
}

static syscall_result_t handle_ipc_trysend(const syscall_args_t *args)
{
    ipc_handle_t          h;
    const ipc_message_t  *msg;

    h   = (ipc_handle_t)(args->arg0 & 0xFFFFFFFFu);
    msg = (const ipc_message_t *)(uintptr_t)args->arg1;

    return ipc_result_to_syscall_result(ipc_trysend(h, msg));
}

static syscall_result_t handle_ipc_tryrecv(const syscall_args_t *args)
{
    ipc_handle_t    h;
    uint32_t        type_filter;
    ipc_message_t  *msg_out;

    h           = (ipc_handle_t)(args->arg0 & 0xFFFFFFFFu);
    type_filter = (uint32_t)(args->arg1 & 0xFFFFFFFFu);
    msg_out     = (ipc_message_t *)(uintptr_t)args->arg2;

    return ipc_result_to_syscall_result(ipc_tryrecv(h, type_filter, msg_out));
}

/* ---------------------------------------------------------------------- */
/* Handlers - SYSCALL_DIAG_WRITE                                         */
/* ---------------------------------------------------------------------- */

static syscall_result_t handle_diag_write(const syscall_args_t *args)
{
    /*
     * arg0 is a virtual address of a NUL-terminated string.
     * Phase 2B: no virtual memory mapping yet, treat as a direct physical
     * address (identity-mapped in early kernel boot).
     *
     * Security note: this syscall is diagnostic only. In a real secure
     * kernel it would be gated by privilege level checks. For now we only
     * print a notice; we do not dereference untrusted pointers in Phase 2B.
     */
    console_write("syscall         : DIAG_WRITE (Phase 2B: print ack only)\n");
    (void)args;
    return SYSCALL_OK;
}

/* ---------------------------------------------------------------------- */
/* Handlers - SYSCALL_DIAG_DUMP_SCHED                                    */
/* ---------------------------------------------------------------------- */

static syscall_result_t handle_diag_dump_sched(const syscall_args_t *args)
{
    (void)args;
    sched_print_queues();
    return SYSCALL_OK;
}

/* ---------------------------------------------------------------------- */
/* Dispatch table entry                                                    */
/* ---------------------------------------------------------------------- */

typedef syscall_result_t (*syscall_handler_fn_t)(const syscall_args_t *args);

typedef struct {
    uint64_t              number;
    syscall_handler_fn_t  handler;
} syscall_table_entry_t;

static const syscall_table_entry_t s_table[] = {
    { SYSCALL_YIELD,                handle_yield               },
    { SYSCALL_EXIT,                 handle_exit                },
    { SYSCALL_GET_PID,              handle_get_pid             },
    { SYSCALL_GET_TID,              handle_get_tid             },
    { SYSCALL_SCHED_BLOCK,          handle_sched_block         },
    { SYSCALL_SCHED_SET_PRIORITY,   handle_sched_set_priority  },
    { SYSCALL_SCHED_SLEEP,          handle_sched_sleep         },
    { SYSCALL_SCHED_GET_PRIORITY,   handle_sched_get_priority  },
    { SYSCALL_IPC_SEND,             handle_ipc_send            },
    { SYSCALL_IPC_RECV,             handle_ipc_recv            },
    { SYSCALL_IPC_TRYSEND,          handle_ipc_trysend         },
    { SYSCALL_IPC_TRYRECV,          handle_ipc_tryrecv         },
    { SYSCALL_DIAG_WRITE,           handle_diag_write          },
    { SYSCALL_DIAG_DUMP_SCHED,      handle_diag_dump_sched     },
};

#define TABLE_COUNT  (sizeof(s_table) / sizeof(s_table[0]))

/* ---------------------------------------------------------------------- */
/* syscall_init                                                            */
/* ---------------------------------------------------------------------- */

void syscall_init(void)
{
    console_write("Syscall         : dispatch table initialized (");
    console_write_dec((uint64_t)TABLE_COUNT);
    console_write(" entries)\n");
}

/* ---------------------------------------------------------------------- */
/* syscall_dispatch                                                        */
/* ---------------------------------------------------------------------- */

syscall_result_t syscall_dispatch(const syscall_args_t *args)
{
    size_t i;
    syscall_result_t result;

    if (args == NULL) {
        return SYSCALL_ERR_INVAL;
    }

    syscall_trace_entry(args->number);

    for (i = 0; i < TABLE_COUNT; i++) {
        if (s_table[i].number == args->number) {
            result = s_table[i].handler(args);
            syscall_trace_result(result);
            return result;
        }
    }

    console_write("syscall         : NOSYS nr=0x");
    console_write_hex64(args->number);
    console_write("\n");
    return SYSCALL_ERR_NOSYS;
}

