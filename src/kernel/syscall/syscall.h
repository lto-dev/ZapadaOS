/*
 * Zapada - src/kernel/syscall/syscall.h
 *
 * Syscall ABI contract and dispatch skeleton for Phase 2B/2C.
 *
 * Design:
 *   - Managed-first expansion: syscall numbers are allocated in bands with
 *     the lower range reserved for kernel primitives and the upper range
 *     open for managed-layer service dispatch.
 *   - Architecture-neutral dispatch: the arch-specific entry points
 *     (SYSCALL/SYSENTER on x86_64, SVC on AArch64) funnel into
 *     syscall_dispatch() with a normalized syscall_args_t.
 *   - Narrow native boundary: only HAL/hardware operations implemented
 *     here. Policy and service logic belong in managed code.
 *   - Deterministic diagnostics: every dispatch logs call number on entry
 *     and result on exit via the console.
 *
 * Syscall number bands:
 *   0x0000 - 0x00FF : Kernel primitives (process, thread, IPC primitives)
 *   0x0100 - 0x01FF : Scheduler interface (Phase 2C additions: 0x0102-0x0104)
 *   0x0200 - 0x02FF : IPC channels
 *   0x0300 - 0xEFFF : Reserved for managed service expansion
 *   0xF000 - 0xFFFF : Diagnostic / debug syscalls
 */

#ifndef ZAPADA_SYSCALL_H
#define ZAPADA_SYSCALL_H

#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Syscall number definitions                                              */
/* ---------------------------------------------------------------------- */

/* Kernel primitive syscalls */
#define SYSCALL_YIELD           0x0001u   /* Yield the current thread */
#define SYSCALL_EXIT            0x0002u   /* Terminate the calling process */
#define SYSCALL_GET_PID         0x0003u   /* Return current process ID */
#define SYSCALL_GET_TID         0x0004u   /* Return current thread ID */

/* Scheduler interface syscalls */
#define SYSCALL_SCHED_BLOCK         0x0101u   /* Block the current thread */
#define SYSCALL_SCHED_SET_PRIORITY  0x0102u   /* Set calling thread's priority */
#define SYSCALL_SCHED_SLEEP         0x0103u   /* Yield for arg0 ticks (Phase 2C: cooperative) */
#define SYSCALL_SCHED_GET_PRIORITY  0x0104u   /* Get calling thread's current priority */

/* IPC syscalls */
#define SYSCALL_IPC_SEND        0x0201u   /* Send a message on a channel */
#define SYSCALL_IPC_RECV        0x0202u   /* Receive a message from a channel */
#define SYSCALL_IPC_TRYSEND     0x0203u   /* Non-blocking send */
#define SYSCALL_IPC_TRYRECV     0x0204u   /* Non-blocking receive */

/* Diagnostic syscalls */
#define SYSCALL_DIAG_WRITE      0xF001u   /* Write a string to the console */
#define SYSCALL_DIAG_DUMP_SCHED 0xF002u   /* Dump scheduler state */

/* ---------------------------------------------------------------------- */
/* Syscall result codes                                                    */
/* ---------------------------------------------------------------------- */

typedef int64_t syscall_result_t;

#define SYSCALL_OK              ((syscall_result_t)  0)
#define SYSCALL_ERR_UNKNOWN     ((syscall_result_t) -1)
#define SYSCALL_ERR_INVAL       ((syscall_result_t) -2)   /* Invalid argument */
#define SYSCALL_ERR_PERM        ((syscall_result_t) -3)   /* Permission denied */
#define SYSCALL_ERR_AGAIN       ((syscall_result_t) -4)   /* Try again (non-blocking) */
#define SYSCALL_ERR_FULL        ((syscall_result_t) -5)   /* Queue full */
#define SYSCALL_ERR_EMPTY       ((syscall_result_t) -6)   /* Queue empty */
#define SYSCALL_ERR_NOSYS       ((syscall_result_t) -7)   /* Syscall not implemented */

/* ---------------------------------------------------------------------- */
/* Syscall argument frame                                                  */
/*                                                                         */
/* Architecture-neutral register frame passed to syscall_dispatch().      */
/* The arch entry stub populates this from the CPU register state.        */
/*                                                                         */
/* Convention (mirrors Linux-style ABI for familiarity):                  */
/*   number - syscall number (from rax/x8 depending on arch)              */
/*   arg0   - first argument  (rdi/x0)                                    */
/*   arg1   - second argument (rsi/x1)                                    */
/*   arg2   - third argument  (rdx/x2)                                    */
/*   arg3   - fourth argument (rcx/x3)                                    */
/*   arg4   - fifth argument  (r8/x4)                                     */
/* ---------------------------------------------------------------------- */

typedef struct {
    uint64_t number;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
} syscall_args_t;

/* ---------------------------------------------------------------------- */
/* Syscall dispatch API                                                    */
/* ---------------------------------------------------------------------- */

/*
 * syscall_init - initialize the syscall dispatch table.
 *
 * Must be called during kernel initialization, after process and scheduler
 * subsystems are ready.
 */
void syscall_init(void);

/*
 * syscall_dispatch - process one syscall.
 *
 * Entry point from the architecture-specific syscall handler. Decodes
 * args->number and dispatches to the appropriate handler. Returns the
 * result code (placed back in rax/x0 by the arch entry stub).
 *
 * Always produces diagnostic output on an unknown syscall.
 */
syscall_result_t syscall_dispatch(const syscall_args_t *args);

#endif /* ZAPADA_SYSCALL_H */


