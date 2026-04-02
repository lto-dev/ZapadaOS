/*
 * Zapada - src/kernel/sched/sched.c
 *
 * Scheduler run queue implementation for Phase 2B/2C.
 *
 * Implements a priority-ordered FIFO run queue backed by a static per-priority
 * linked list (using thread_t.next as the intrusive link).
 *
 * Phase 2C additions:
 *   - s_current: global pointer to the currently executing thread.
 *   - sched_get_current() / sched_set_current(): accessors for s_current.
 *   - sched_context_switch(): invokes architecture-specific assembly to
 *     perform a real register-level CPU context switch.
 *
 * All paths produce deterministic serial diagnostic output.
 */

#include <kernel/sched/sched.h>
#include <kernel/console.h>
#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Internal run queue state                                                */
/* ---------------------------------------------------------------------- */

/* Per-priority queue: head and tail pointers for O(1) enqueue */
typedef struct {
    thread_t *head;
    thread_t *tail;
    uint32_t  count;
} run_queue_t;

static run_queue_t s_queues[SCHED_PRIO_MAX];
static uint32_t    s_total_ready;
static sched_state_t s_state;
static bool        s_preempt_requested;

/* Phase 2C: pointer to the currently executing thread. NULL when idle. */
static thread_t   *s_current;

/* ---------------------------------------------------------------------- */
/* Internal queue helpers                                                  */
/* ---------------------------------------------------------------------- */

static bool queue_contains(const run_queue_t *q, const thread_t *target)
{
    const thread_t *cur;

    if (q == NULL || target == NULL) {
        return false;
    }

    cur = q->head;
    while (cur != NULL) {
        if (cur == target) {
            return true;
        }
        cur = cur->next;
    }

    return false;
}

static bool queue_remove(thread_t *target)
{
    uint32_t     i;
    run_queue_t *q;
    thread_t    *prev;
    thread_t    *cur;

    if (target == NULL) {
        return false;
    }

    for (i = 0; i < SCHED_PRIO_MAX; i++) {
        q = &s_queues[i];
        prev = NULL;
        cur  = q->head;

        while (cur != NULL) {
            if (cur == target) {
                if (prev == NULL) {
                    q->head = cur->next;
                } else {
                    prev->next = cur->next;
                }

                if (q->tail == cur) {
                    q->tail = prev;
                }

                cur->next = NULL;
                q->count--;
                s_total_ready--;

                if (s_total_ready == 0u) {
                    s_state = SCHED_IDLE;
                }

                return true;
            }

            prev = cur;
            cur  = cur->next;
        }
    }

    return false;
}

/* ---------------------------------------------------------------------- */
/* sched_init                                                              */
/* ---------------------------------------------------------------------- */

void sched_init(void)
{
    uint32_t i;

    for (i = 0; i < SCHED_PRIO_MAX; i++) {
        s_queues[i].head  = NULL;
        s_queues[i].tail  = NULL;
        s_queues[i].count = 0;
    }

    s_total_ready       = 0;
    s_state             = SCHED_IDLE;
    s_preempt_requested = false;
    s_current           = NULL;

    console_write("Scheduler       : initialized (");
    console_write_dec((uint64_t)SCHED_PRIO_MAX);
    console_write(" priority bands, quantum=");
    console_write_dec((uint64_t)SCHED_DEFAULT_QUANTUM);
    console_write(" ticks)\n");
}

/* ---------------------------------------------------------------------- */
/* sched_enqueue                                                           */
/* ---------------------------------------------------------------------- */

bool sched_enqueue(thread_t *t)
{
    run_queue_t *q;

    if (t == NULL) {
        console_write("sched_enqueue   : ERROR: NULL thread\n");
        return false;
    }
    if (t->state != PROCESS_STATE_READY) {
        console_write("sched_enqueue   : ERROR: thread not READY (tid=");
        console_write_dec((uint64_t)t->tid);
        console_write(" state=");
        console_write(process_state_name(t->state));
        console_write(")\n");
        return false;
    }
    if (t->priority >= SCHED_PRIO_MAX) {
        console_write("sched_enqueue   : ERROR: priority out of range\n");
        return false;
    }
    if (s_total_ready >= SCHED_MAX_THREADS) {
        console_write("sched_enqueue   : ERROR: run queue full\n");
        return false;
    }

    q = &s_queues[t->priority];

    if (queue_contains(q, t)) {
        console_write("sched_enqueue   : ERROR: thread already queued (tid=");
        console_write_dec((uint64_t)t->tid);
        console_write(")\n");
        return false;
    }

    t->next = NULL;

    if (q->tail == NULL) {
        /* Queue was empty */
        q->head = t;
        q->tail = t;
    } else {
        q->tail->next = t;
        q->tail       = t;
    }

    q->count++;
    s_total_ready++;
    s_state = SCHED_RUNNING;

    return true;
}

/* ---------------------------------------------------------------------- */
/* sched_dequeue                                                           */
/* ---------------------------------------------------------------------- */

thread_t *sched_dequeue(void)
{
    uint32_t    i;
    run_queue_t *q;
    thread_t    *t;

    for (i = 0; i < SCHED_PRIO_MAX; i++) {
        q = &s_queues[i];
        if (q->head == NULL) {
            continue;
        }

        t = q->head;
        q->head = t->next;
        if (q->head == NULL) {
            q->tail = NULL;
        }
        t->next = NULL;

        q->count--;
        s_total_ready--;

        if (s_total_ready == 0) {
            s_state = SCHED_IDLE;
        }

        return t;
    }

    return NULL;
}

/* ---------------------------------------------------------------------- */
/* sched_yield                                                             */
/* ---------------------------------------------------------------------- */

bool sched_yield(thread_t *current)
{
    if (current == NULL) {
        console_write("sched_yield     : ERROR: NULL thread\n");
        return false;
    }
    if (current->state != PROCESS_STATE_RUNNING) {
        console_write("sched_yield     : ERROR: thread not RUNNING\n");
        return false;
    }

    current->state           = PROCESS_STATE_READY;
    current->ticks_remaining = SCHED_DEFAULT_QUANTUM;
    s_preempt_requested      = false;

    return sched_enqueue(current);
}

/* ---------------------------------------------------------------------- */
/* sched_tick                                                              */
/* ---------------------------------------------------------------------- */

void sched_tick(thread_t *current)
{
    if (current == NULL) {
        return;
    }

    current->total_ticks++;

    if (current->ticks_remaining > 0) {
        current->ticks_remaining--;
    }

    if (current->ticks_remaining == 0) {
        s_preempt_requested = true;
        s_state = SCHED_PREEMPT;
    }
}

/* ---------------------------------------------------------------------- */
/* sched_next                                                              */
/* ---------------------------------------------------------------------- */

thread_t *sched_next(thread_t *current)
{
    thread_t *next;

    /*
     * If preemption was requested and current is still RUNNING, re-queue it
     * as READY before selecting the next thread.
     */
    if (s_preempt_requested && current != NULL &&
        current->state == PROCESS_STATE_RUNNING) {
        s_preempt_requested = false;
        current->state           = PROCESS_STATE_READY;
        current->ticks_remaining = SCHED_DEFAULT_QUANTUM;
        (void)sched_enqueue(current);
        current = NULL;
    }

    next = sched_dequeue();

    if (next == NULL) {
        /*
         * No other runnable thread. If current is still conceptually RUNNING,
         * let it continue (return it). Otherwise idle.
         */
        if (current != NULL && current->state == PROCESS_STATE_RUNNING) {
            return current;
        }
        s_state = SCHED_IDLE;
        return NULL;
    }

    /* Set initial quantum if not already assigned */
    if (next->ticks_remaining == 0) {
        next->ticks_remaining = SCHED_DEFAULT_QUANTUM;
    }

    return next;
}

/* ---------------------------------------------------------------------- */
/* sched_block                                                             */
/* ---------------------------------------------------------------------- */

bool sched_block(thread_t *t)
{
    if (t == NULL) {
        console_write("sched_block     : ERROR: NULL thread\n");
        return false;
    }
    if (t->state == PROCESS_STATE_BLOCKED) {
        return true;  /* Already blocked - idempotent */
    }

    if (t->state == PROCESS_STATE_READY) {
        if (!queue_remove(t)) {
            console_write("sched_block     : ERROR: READY thread not found in run queue (tid=");
            console_write_dec((uint64_t)t->tid);
            console_write(")\n");
            return false;
        }
    } else if (t->state != PROCESS_STATE_RUNNING) {
        console_write("sched_block     : ERROR: thread not READY/RUNNING (tid=");
        console_write_dec((uint64_t)t->tid);
        console_write(" state=");
        console_write(process_state_name(t->state));
        console_write(")\n");
        return false;
    }

    t->state = PROCESS_STATE_BLOCKED;
    return true;
}

/* ---------------------------------------------------------------------- */
/* sched_unblock                                                           */
/* ---------------------------------------------------------------------- */

bool sched_unblock(thread_t *t)
{
    if (t == NULL) {
        console_write("sched_unblock   : ERROR: NULL thread\n");
        return false;
    }
    if (t->state != PROCESS_STATE_BLOCKED) {
        console_write("sched_unblock   : ERROR: thread not BLOCKED (tid=");
        console_write_dec((uint64_t)t->tid);
        console_write(")\n");
        return false;
    }

    t->state = PROCESS_STATE_READY;
    return sched_enqueue(t);
}

/* ---------------------------------------------------------------------- */
/* sched_get_state                                                         */
/* ---------------------------------------------------------------------- */

sched_state_t sched_get_state(void)
{
    return s_state;
}

/* ---------------------------------------------------------------------- */
/* sched_get_ready_count                                                   */
/* ---------------------------------------------------------------------- */

uint32_t sched_get_ready_count(void)
{
    return s_total_ready;
}

/* ---------------------------------------------------------------------- */
/* sched_print_queues                                                      */
/* ---------------------------------------------------------------------- */

void sched_print_queues(void)
{
    uint32_t  i;
    thread_t *t;

    console_write("Scheduler queues:\n");
    for (i = 0; i < SCHED_PRIO_MAX; i++) {
        if (s_queues[i].count == 0) {
            continue;
        }
        console_write("  prio[");
        console_write_dec((uint64_t)i);
        console_write("] count=");
        console_write_dec((uint64_t)s_queues[i].count);
        console_write(" threads:");
        t = s_queues[i].head;
        while (t != NULL) {
            console_write(" tid=");
            console_write_dec((uint64_t)t->tid);
            console_write("(");
            console_write(t->name);
            console_write(")");
            t = t->next;
        }
        console_write("\n");
    }
    console_write("  total ready: ");
    console_write_dec((uint64_t)s_total_ready);
    console_write("\n");
}

/* ---------------------------------------------------------------------- */
/* Phase 2C: Architecture-specific context switch declarations             */
/* ---------------------------------------------------------------------- */

#if defined(__x86_64__)
/*
 * sched_x86_context_switch(prev_ctx, next_ctx)
 *   Defined in src/kernel/arch/x86_64/context_switch.asm.
 *   Saves callee-saved registers on prev's stack, stores RSP into
 *   prev_ctx->sp, loads RSP from next_ctx->sp, restores callee-saved
 *   registers from next's stack, then RET into next's execution context.
 */
extern void sched_x86_context_switch(cpu_context_t *prev_ctx,
                                      cpu_context_t *next_ctx);
#elif defined(__aarch64__)
/*
 * sched_aa64_context_switch(prev_ctx, next_ctx)
 *   Defined in src/kernel/arch/aarch64/context_switch.S.
 *   Saves x19-x30 on prev's stack, stores SP into prev_ctx->sp, loads
 *   SP from next_ctx->sp, restores x19-x30 from next's stack, then RET.
 */
extern void sched_aa64_context_switch(cpu_context_t *prev_ctx,
                                       cpu_context_t *next_ctx);
#endif

/* ---------------------------------------------------------------------- */
/* sched_get_current                                                       */
/* ---------------------------------------------------------------------- */

thread_t *sched_get_current(void)
{
    return s_current;
}

/* ---------------------------------------------------------------------- */
/* sched_set_current                                                       */
/* ---------------------------------------------------------------------- */

void sched_set_current(thread_t *t)
{
    s_current = t;
}

/* ---------------------------------------------------------------------- */
/* sched_context_switch                                                    */
/* ---------------------------------------------------------------------- */

void sched_context_switch(thread_t *prev, thread_t *next)
{
    if (prev == NULL || next == NULL) {
        console_write("sched_cs        : ERROR: NULL prev or next\n");
        return;
    }

    /*
     * Mark next as RUNNING and record it as the current thread before
     * executing the switch. If prev == next (switch to self), this is a
     * no-op at the register level but state is still consistent.
     */
    next->state = PROCESS_STATE_RUNNING;
    s_current   = next;

#if defined(__x86_64__)
    sched_x86_context_switch(&prev->ctx, &next->ctx);
#elif defined(__aarch64__)
    sched_aa64_context_switch(&prev->ctx, &next->ctx);
#else
    /*
     * Unsupported architecture: no-op. Execution continues in prev's context.
     */
    (void)prev;
    (void)next;
    console_write("sched_cs        : WARNING: no context switch for this arch\n");
#endif
}

