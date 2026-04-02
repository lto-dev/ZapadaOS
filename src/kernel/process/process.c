/*
 * Zapada - src/kernel/process/process.c
 *
 * Kernel process and thread lifecycle implementation for Phase 2B.
 *
 * All policy remains in managed code. These functions implement only the
 * hardware-adjacent structural operations: state transitions, slot management,
 * and diagnostic output.
 *
 * Diagnostic output uses console_write so that both x86_64 (serial) and
 * AArch64 (UART via console.h) share the same output path.
 */

#include <kernel/process/process.h>
#include <kernel/console.h>
#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Internal helpers                                                        */
/* ---------------------------------------------------------------------- */

static void str_copy_n(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n - 1u && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void thread_zero(thread_t *t)
{
    uint32_t i;
    uint8_t *p = (uint8_t *)t;
    for (i = 0; i < (uint32_t)sizeof(thread_t); i++) {
        p[i] = 0;
    }
}

static void process_zero(process_t *p)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)p;
    for (i = 0; i < (uint32_t)sizeof(process_t); i++) {
        b[i] = 0;
    }
}

/* ---------------------------------------------------------------------- */
/* process_state_name                                                      */
/* ---------------------------------------------------------------------- */

const char *process_state_name(process_state_t state)
{
    switch (state) {
        case PROCESS_STATE_INIT:    return "INIT";
        case PROCESS_STATE_READY:   return "READY";
        case PROCESS_STATE_RUNNING: return "RUNNING";
        case PROCESS_STATE_BLOCKED: return "BLOCKED";
        case PROCESS_STATE_ZOMBIE:  return "ZOMBIE";
        default:                    return "UNKNOWN";
    }
}

/* ---------------------------------------------------------------------- */
/* process_init                                                            */
/* ---------------------------------------------------------------------- */

bool process_init(process_t *p, pid_t pid, const char *name)
{
    uint32_t i;

    if (p == NULL || name == NULL) {
        console_write("process_init    : ERROR: NULL argument\n");
        return false;
    }

    process_zero(p);

    p->pid   = pid;
    p->state = PROCESS_STATE_INIT;
    p->exit_code    = 0;
    p->thread_count = 0;
    p->main_thread  = NULL;
    p->next         = NULL;

    str_copy_n(p->name, name, PROCESS_NAME_MAX);

    /* Mark all thread slots as unoccupied */
    for (i = 0; i < PROCESS_MAX_THREADS; i++) {
        p->threads[i].tid   = TID_INVALID;
        p->threads[i].state = PROCESS_STATE_INIT;
    }

    return true;
}

/* ---------------------------------------------------------------------- */
/* process_add_thread                                                      */
/* ---------------------------------------------------------------------- */

thread_t *process_add_thread(process_t *p, uint32_t index, tid_t tid,
                              const char *name, uint32_t priority)
{
    thread_t *t;

    if (p == NULL) {
        console_write("process_add_thread : ERROR: NULL process\n");
        return NULL;
    }
    if (index >= PROCESS_MAX_THREADS) {
        console_write("process_add_thread : ERROR: index out of range\n");
        return NULL;
    }
    if (tid == TID_INVALID) {
        console_write("process_add_thread : ERROR: TID_INVALID not allowed\n");
        return NULL;
    }
    if (name == NULL) {
        console_write("process_add_thread : ERROR: NULL name\n");
        return NULL;
    }

    t = &p->threads[index];

    /* Slot must be unoccupied */
    if (t->tid != TID_INVALID) {
        console_write("process_add_thread : ERROR: slot already occupied\n");
        return NULL;
    }

    thread_zero(t);
    t->tid             = tid;
    t->state           = PROCESS_STATE_INIT;
    t->priority        = priority;
    t->ticks_remaining = 0;
    t->total_ticks     = 0;
    t->managed_ctx     = NULL;
    t->next            = NULL;

    str_copy_n(t->name, name, THREAD_NAME_MAX);

    p->thread_count++;

    if (p->main_thread == NULL) {
        p->main_thread = t;
    }

    return t;
}

/* ---------------------------------------------------------------------- */
/* process_ready                                                           */
/* ---------------------------------------------------------------------- */

bool process_ready(process_t *p)
{
    if (p == NULL) {
        console_write("process_ready   : ERROR: NULL process\n");
        return false;
    }
    if (p->state != PROCESS_STATE_INIT) {
        console_write("process_ready   : ERROR: process not in INIT state\n");
        return false;
    }

    p->state = PROCESS_STATE_READY;

    if (p->main_thread != NULL && p->main_thread->state == PROCESS_STATE_INIT) {
        p->main_thread->state = PROCESS_STATE_READY;
    }

    return true;
}

/* ---------------------------------------------------------------------- */
/* process_exit                                                            */
/* ---------------------------------------------------------------------- */

bool process_exit(process_t *p, int32_t exit_code)
{
    uint32_t i;

    if (p == NULL) {
        console_write("process_exit    : ERROR: NULL process\n");
        return false;
    }

    p->exit_code = exit_code;
    p->state     = PROCESS_STATE_ZOMBIE;

    /* Transition all non-zombie threads to zombie */
    for (i = 0; i < PROCESS_MAX_THREADS; i++) {
        if (p->threads[i].tid != TID_INVALID &&
            p->threads[i].state != PROCESS_STATE_ZOMBIE) {
            p->threads[i].state = PROCESS_STATE_ZOMBIE;
        }
    }

    return true;
}

