/*
 * Zapada - src/kernel/process/process.h
 *
 * Kernel process and thread core structures for Phase 2B/2C.
 *
 * Design constraints:
 *   - Architecture-neutral: no arch-specific registers or ABI in this header.
 *   - Managed-first: the process model is designed to host managed (CIL)
 *     execution contexts. Native threads are only used for privilege/hardware
 *     primitives.
 *   - No dynamic allocator dependency in type definitions: all structures use
 *     fixed-size fields. Allocation is done by callers (process_create).
 *   - Aligned to 8 bytes on AArch64 (mstrict-align discipline enforced).
 *
 * Lifecycle state machine:
 *
 *   PROCESS_STATE_INIT
 *       |
 *       v (process_init)
 *   PROCESS_STATE_READY <-------+
 *       |                       |
 *       v (scheduler picks)     |
 *   PROCESS_STATE_RUNNING       | (yield / preempt)
 *       |                       |
 *       +---> PROCESS_STATE_BLOCKED
 *       |           |
 *       |           v (unblock)
 *       |       PROCESS_STATE_READY
 *       |
 *       v (process_exit)
 *   PROCESS_STATE_ZOMBIE
 *       |
 *       v (process_destroy)
 *   (freed)
 *
 * Thread lifecycle mirrors process lifecycle per-thread.
 */

#ifndef ZAPADA_PROCESS_H
#define ZAPADA_PROCESS_H

#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Process and thread ID types                                             */
/* ---------------------------------------------------------------------- */

typedef uint32_t pid_t;   /* Process ID */
typedef uint32_t tid_t;   /* Thread ID  */

#define PID_INVALID  ((pid_t)0)
#define TID_INVALID  ((tid_t)0)
#define PID_KERNEL   ((pid_t)1)   /* Reserved PID for the kernel idle process */

/* ---------------------------------------------------------------------- */
/* Process/thread lifecycle states                                         */
/* ---------------------------------------------------------------------- */

typedef enum {
    PROCESS_STATE_INIT    = 0,  /* Structure allocated, not yet ready */
    PROCESS_STATE_READY   = 1,  /* In run queue, waiting for CPU */
    PROCESS_STATE_RUNNING = 2,  /* Currently executing on a CPU */
    PROCESS_STATE_BLOCKED = 3,  /* Waiting for an event (IPC, timer) */
    PROCESS_STATE_ZOMBIE  = 4,  /* Terminated, awaiting cleanup */
} process_state_t;

/* ---------------------------------------------------------------------- */
/* Saved CPU context                                                       */
/*                                                                         */
/* Phase 2C: only sp is live. ip is stored for diagnostic purposes.       */
/* The actual callee-saved register frame is maintained on the kernel      */
/* stack (see kstack.h / context_switch.asm / context_switch.S).          */
/* ---------------------------------------------------------------------- */

typedef struct {
    uint64_t sp;        /* Saved stack pointer */
    uint64_t ip;        /* Saved instruction pointer */
    uint64_t flags;     /* Saved flags/PSTATE (arch-specific interpretation) */
} cpu_context_t;

/* ---------------------------------------------------------------------- */
/* Thread control block (TCB)                                              */
/* ---------------------------------------------------------------------- */

#define THREAD_NAME_MAX  32

typedef struct thread_t thread_t;

struct thread_t {
    tid_t            tid;                       /* Thread ID (unique per process) */
    process_state_t  state;                     /* Thread lifecycle state */
    cpu_context_t    ctx;                       /* Saved CPU context (sp is live) */

    uint32_t         priority;                  /* Scheduling priority (0 = highest) */
    uint64_t         ticks_remaining;           /* Preemption countdown in scheduler ticks */
    uint64_t         total_ticks;               /* Lifetime tick count */

    /*
     * Kernel stack linkage (Phase 2C).
     * kstack_base: base (bottom) address of this thread's kernel stack.
     * Derived from kstack_alloc() return value (top) minus KSTACK_SIZE.
     * Used by kstack_free() on thread destruction.
     * NULL for threads that do not own an allocated kernel stack.
     */
    void            *kstack_base;

    /*
     * Managed runtime linkage. When non-NULL, this thread is hosting a
     * managed (CIL) execution context. Treated as an opaque cookie by the
     * kernel; the CLR layer owns the pointer.
     */
    void            *managed_ctx;

    char             name[THREAD_NAME_MAX];     /* Human-readable thread name */

    thread_t        *next;                      /* Run-queue intrusive list link */
};

/* ---------------------------------------------------------------------- */
/* Process control block (PCB)                                             */
/* ---------------------------------------------------------------------- */

#define PROCESS_NAME_MAX    32
#define PROCESS_MAX_THREADS 4   /* Phase 2B: bounded per-process thread count */

typedef struct process_t process_t;

struct process_t {
    pid_t            pid;                           /* Process ID */
    process_state_t  state;                         /* Aggregate process state */

    uint32_t         thread_count;                  /* Current live thread count */
    thread_t         threads[PROCESS_MAX_THREADS];  /* Inline thread array */
    thread_t        *main_thread;                   /* Pointer into threads[] */

    int32_t          exit_code;                     /* Set on ZOMBIE transition */

    char             name[PROCESS_NAME_MAX];        /* Human-readable process name */

    process_t       *next;                          /* Global process list link */
};

/* ---------------------------------------------------------------------- */
/* Process lifecycle API                                                   */
/* ---------------------------------------------------------------------- */

/*
 * process_init - initialize a pre-allocated process_t to INIT state.
 *
 * The caller provides the storage. process_init zeroes the structure and
 * assigns the pid, name, and initial state. The pid must be unique; callers
 * are responsible for ID allocation.
 *
 * Returns true on success, false if p is NULL or name is NULL.
 */
bool process_init(process_t *p, pid_t pid, const char *name);

/*
 * process_add_thread - initialize thread slot [index] in the process and
 * return a pointer to it. The thread starts in INIT state.
 *
 * Returns NULL if p is NULL, index >= PROCESS_MAX_THREADS, or the slot is
 * already occupied (state != INIT with tid != TID_INVALID).
 */
thread_t *process_add_thread(process_t *p, uint32_t index, tid_t tid,
                              const char *name, uint32_t priority);

/*
 * process_ready - transition a process (and its main thread) to READY state.
 * The process must currently be in INIT state.
 *
 * Returns true on success, false if state is not INIT or p is NULL.
 */
bool process_ready(process_t *p);

/*
 * process_exit - mark the process as ZOMBIE with the given exit code.
 * Transitions all non-zombie threads to ZOMBIE state as well.
 *
 * Returns true on success.
 */
bool process_exit(process_t *p, int32_t exit_code);

/*
 * process_state_name - return a human-readable string for a process_state_t.
 */
const char *process_state_name(process_state_t state);

#endif /* ZAPADA_PROCESS_H */


