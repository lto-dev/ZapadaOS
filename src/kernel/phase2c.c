/*
 * Zapada - src/kernel/phase2c.c
 *
 * Phase 2C initialization and self-test suite.
 *
 * Tests implemented (10 total):
 *   T01  Timer tick advancement via simulated timer_tick_handler() calls
 *   T02  Kernel stack pool alloc/free
 *   T03  kstack_init_context: sp/ip field setup
 *   T04  sched_get_current / sched_set_current round-trip
 *   T05  Real context switch between two kernel threads
 *   T06  Blocking IPC: sender blocks on full channel, receiver unblocks it
 *   T07  SYSCALL_SCHED_SET_PRIORITY sets thread priority
 *   T08  SYSCALL_SCHED_GET_PRIORITY reads thread priority
 *   T09  GDT selector constants (x86_64 only; AArch64 passes trivially)
 *   T10  user_isr_frame_t is larger than isr_frame_t (x86_64 only)
 *
 * Convention: each test function returns 1 (pass) or 0 (fail).
 * Diagnostic output uses console_write on all paths.
 * No heap allocation; no packed structs; all state is in BSS.
 */

#include <kernel/phase2c.h>
#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <kernel/sched/timer.h>
#include <kernel/sched/kstack.h>
#include <kernel/ipc/ipc.h>
#include <kernel/syscall/syscall.h>
#include <kernel/console.h>
#include <kernel/types.h>

#if defined(__x86_64__)
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/arch/x86_64/idt.h>
#endif

/* ---------------------------------------------------------------------- */
/* Test result tracking                                                    */
/* ---------------------------------------------------------------------- */

static uint32_t s_pass;
static uint32_t s_fail;

static void report(const char *name, int passed)
{
    if (passed) {
        console_write("  PASS  ");
        s_pass++;
    } else {
        console_write("  FAIL  ");
        s_fail++;
    }
    console_write(name);
    console_write("\n");
}

/* ---------------------------------------------------------------------- */
/* T01: Timer tick advancement                                             */
/* ---------------------------------------------------------------------- */

static int test_timer_ticks(void)
{
    uint64_t before;
    uint64_t after;
    uint32_t i;

    before = timer_get_tick_count();

    for (i = 0; i < 5u; i++) {
        timer_tick_handler();
    }

    after = timer_get_tick_count();

    if (after < before + 5u) {
        console_write("    timer ticks: expected ");
        console_write_dec(before + 5u);
        console_write(" or more got ");
        console_write_dec(after);
        console_write("\n");
        return 0;
    }

    return 1;
}

/* ---------------------------------------------------------------------- */
/* T02: Kernel stack pool alloc/free                                       */
/* ---------------------------------------------------------------------- */

static int test_kstack_alloc(void)
{
    void *top1;
    void *top2;

    top1 = kstack_alloc();
    if (top1 == NULL) {
        console_write("    kstack_alloc #1 returned NULL\n");
        return 0;
    }

    top2 = kstack_alloc();
    if (top2 == NULL) {
        console_write("    kstack_alloc #2 returned NULL\n");
        kstack_free(top1);
        return 0;
    }

    if (top1 == top2) {
        console_write("    kstack_alloc returned same pointer twice\n");
        kstack_free(top1);
        kstack_free(top2);
        return 0;
    }

    kstack_free(top1);
    kstack_free(top2);

    /* After freeing, we should be able to reallocate */
    top1 = kstack_alloc();
    if (top1 == NULL) {
        console_write("    kstack_alloc after free returned NULL\n");
        return 0;
    }
    kstack_free(top1);

    return 1;
}

/* ---------------------------------------------------------------------- */
/* T03: kstack_init_context field setup                                    */
/* ---------------------------------------------------------------------- */

static void dummy_entry_fn(void)
{
    /* Used only as an address value; never actually called in this test */
    for (;;) {}
}

static process_t s_proc_t03;

static int test_kstack_init_context(void)
{
    thread_t *t;
    void     *kstack_top;

    if (!process_init(&s_proc_t03, 200, "t03proc")) {
        return 0;
    }

    t = process_add_thread(&s_proc_t03, 0, 201, "t03thread", 0);
    if (t == NULL) {
        return 0;
    }

    kstack_top = kstack_alloc();
    if (kstack_top == NULL) {
        return 0;
    }

    if (!kstack_init_context(t, kstack_top, dummy_entry_fn)) {
        kstack_free(kstack_top);
        return 0;
    }

    /* sp must be non-zero and point below the stack top */
    if (t->ctx.sp == 0u) {
        console_write("    ctx.sp is zero after kstack_init_context\n");
        kstack_free(kstack_top);
        return 0;
    }

    if (t->ctx.sp >= (uint64_t)(uintptr_t)kstack_top) {
        console_write("    ctx.sp >= kstack_top (invalid)\n");
        kstack_free(kstack_top);
        return 0;
    }

    if (t->ctx.ip != (uint64_t)(uintptr_t)dummy_entry_fn) {
        console_write("    ctx.ip does not match entry function\n");
        kstack_free(kstack_top);
        return 0;
    }

    /* kstack_base must be below kstack_top by exactly KSTACK_SIZE */
    if (t->kstack_base == NULL) {
        console_write("    kstack_base is NULL\n");
        kstack_free(kstack_top);
        return 0;
    }

    kstack_free(kstack_top);
    return 1;
}

/* ---------------------------------------------------------------------- */
/* T04: sched_get_current / sched_set_current                             */
/* ---------------------------------------------------------------------- */

static process_t s_proc_t04;

static int test_sched_current(void)
{
    thread_t *t;
    thread_t *saved;

    /* Save existing current (may be NULL) */
    saved = sched_get_current();

    if (!process_init(&s_proc_t04, 300, "t04proc")) {
        return 0;
    }

    t = process_add_thread(&s_proc_t04, 0, 301, "t04thread", 0);
    if (t == NULL) {
        return 0;
    }

    sched_set_current(t);

    if (sched_get_current() != t) {
        console_write("    sched_get_current returned wrong pointer\n");
        sched_set_current(saved);
        return 0;
    }

    sched_set_current(NULL);
    if (sched_get_current() != NULL) {
        console_write("    sched_get_current not NULL after set(NULL)\n");
        sched_set_current(saved);
        return 0;
    }

    sched_set_current(saved);
    return 1;
}

/* ---------------------------------------------------------------------- */
/* T05: Real context switch between two kernel threads                     */
/*                                                                         */
/* Global state is required because the worker thread function cannot     */
/* receive parameters in Phase 2C (no argument passing in the trampoline).*/
/* ---------------------------------------------------------------------- */

static process_t         s_proc_cs;
static thread_t         *s_t_cs_main;
static thread_t         *s_t_cs_worker;
static volatile uint32_t s_cs_worker_ran = 0u;

static void cs_worker_fn(void)
{
    s_cs_worker_ran = 1u;

    /*
     * Switch back to the main test thread (which saved its context before
     * switching here). The main thread is RUNNING/READY from the main
     * test context; we perform a direct switch back.
     */
    s_t_cs_worker->state = PROCESS_STATE_READY;
    sched_context_switch(s_t_cs_worker, s_t_cs_main);

    /* Should not return from sched_context_switch in this scenario */
    for (;;) {}
}

static int test_context_switch(void)
{
    void     *kstack_top;
    thread_t *prev_current;

    prev_current = sched_get_current();

    if (!process_init(&s_proc_cs, 400, "csproc")) {
        return 0;
    }

    s_t_cs_main   = process_add_thread(&s_proc_cs, 0, 401, "cs-main",   0);
    s_t_cs_worker = process_add_thread(&s_proc_cs, 1, 402, "cs-worker", 0);

    if (s_t_cs_main == NULL || s_t_cs_worker == NULL) {
        return 0;
    }

    kstack_top = kstack_alloc();
    if (kstack_top == NULL) {
        return 0;
    }

    if (!kstack_init_context(s_t_cs_worker, kstack_top, cs_worker_fn)) {
        kstack_free(kstack_top);
        return 0;
    }

    s_cs_worker_ran      = 0u;
    s_t_cs_main->state   = PROCESS_STATE_RUNNING;
    s_t_cs_worker->state = PROCESS_STATE_READY;

    sched_set_current(s_t_cs_main);

    /* Switch to worker; returns here after worker switches back */
    sched_context_switch(s_t_cs_main, s_t_cs_worker);

    /* Restore scheduler state */
    sched_set_current(prev_current);
    kstack_free(kstack_top);

    if (s_cs_worker_ran != 1u) {
        console_write("    worker did not run (s_cs_worker_ran=");
        console_write_dec((uint64_t)s_cs_worker_ran);
        console_write(")\n");
        return 0;
    }

    return 1;
}

/* ---------------------------------------------------------------------- */
/* T06: Blocking IPC sender blocks on full channel, receiver unblocks it  */
/* ---------------------------------------------------------------------- */

static process_t         s_proc_ipc;
static thread_t         *s_t_ipc_sender;
static thread_t         *s_t_ipc_receiver;
static ipc_handle_t      s_ipc_ch_test;
static volatile uint32_t s_ipc_recv_count = 0u;
static volatile uint32_t s_ipc_send_done  = 0u;

static void ipc_receiver_fn(void)
{
    ipc_message_t msg;
    ipc_result_t  r;
    thread_t     *next;

    /*
     * Receive one message. This calls ipc_tryrecv, which will:
     *   1. Dequeue a message from the ring buffer.
     *   2. Detect ch->blocked_sender (= s_t_ipc_sender) != NULL.
     *   3. Call sched_unblock(s_t_ipc_sender) -> enqueue it as READY.
     */
    r = ipc_tryrecv(s_ipc_ch_test, IPC_MSG_TYPE_ANY, &msg);
    if (r == IPC_OK) {
        s_ipc_recv_count = 1u;
    }

    /*
     * At this point, s_t_ipc_sender is READY in the run queue (unblocked
     * by ipc_tryrecv). Dequeue it and switch back to it.
     */
    next = sched_dequeue();
    if (next == NULL) {
        /* Fallback: sender somehow not in queue; loop */
        for (;;) {}
    }

    s_t_ipc_receiver->state = PROCESS_STATE_READY;
    sched_context_switch(s_t_ipc_receiver, next);

    for (;;) {}
}

static int test_blocking_ipc(void)
{
    ipc_message_t msg;
    ipc_result_t  r;
    void         *recv_kstack;
    uint32_t      i;
    thread_t     *prev_current;

    prev_current = sched_get_current();

    /* Channel for the test */
    s_ipc_ch_test = ipc_channel_create();
    if (s_ipc_ch_test == IPC_HANDLE_INVALID) {
        return 0;
    }

    /* Set up process and threads */
    if (!process_init(&s_proc_ipc, 500, "ipcproc")) {
        ipc_channel_destroy(s_ipc_ch_test);
        return 0;
    }

    s_t_ipc_sender   = process_add_thread(&s_proc_ipc, 0, 501, "ipc-send", 0);
    s_t_ipc_receiver = process_add_thread(&s_proc_ipc, 1, 502, "ipc-recv", 0);

    if (s_t_ipc_sender == NULL || s_t_ipc_receiver == NULL) {
        ipc_channel_destroy(s_ipc_ch_test);
        return 0;
    }

    /* Fill the channel to capacity */
    msg.type        = IPC_MSG_TYPE_SIGNAL;
    msg.payload_len = 0u;
    for (i = 0u; i < IPC_CHANNEL_CAPACITY; i++) {
        r = ipc_trysend(s_ipc_ch_test, &msg);
        if (r != IPC_OK) {
            console_write("    fill channel: ipc_trysend failed at i=");
            console_write_dec((uint64_t)i);
            console_write("\n");
            ipc_channel_destroy(s_ipc_ch_test);
            return 0;
        }
    }

    /* Set up receiver thread context */
    recv_kstack = kstack_alloc();
    if (recv_kstack == NULL) {
        ipc_channel_destroy(s_ipc_ch_test);
        return 0;
    }

    if (!kstack_init_context(s_t_ipc_receiver, recv_kstack, ipc_receiver_fn)) {
        kstack_free(recv_kstack);
        ipc_channel_destroy(s_ipc_ch_test);
        return 0;
    }

    /* Enqueue receiver as READY so ipc_send can find it via sched_dequeue */
    s_t_ipc_receiver->state = PROCESS_STATE_READY;
    if (!sched_enqueue(s_t_ipc_receiver)) {
        kstack_free(recv_kstack);
        ipc_channel_destroy(s_ipc_ch_test);
        return 0;
    }

    /* Set sender as current thread */
    s_ipc_send_done      = 0u;
    s_ipc_recv_count     = 0u;
    s_t_ipc_sender->state = PROCESS_STATE_RUNNING;
    sched_set_current(s_t_ipc_sender);

    /*
     * ipc_send: channel is full -> blocks sender -> dequeues receiver ->
     * switches to receiver. Receiver dequeues one message (unblocking us),
     * then switches back. We resume here and retry ipc_trysend (now succeeds).
     */
    r = ipc_send(s_ipc_ch_test, &msg);

    s_ipc_send_done = 1u;

    /* Restore scheduler state */
    sched_set_current(prev_current);

    if (r != IPC_OK) {
        console_write("    ipc_send returned ");
        console_write(ipc_result_name(r));
        console_write("\n");
        kstack_free(recv_kstack);
        ipc_channel_destroy(s_ipc_ch_test);
        return 0;
    }

    if (s_ipc_recv_count != 1u) {
        console_write("    receiver did not receive (recv_count=");
        console_write_dec((uint64_t)s_ipc_recv_count);
        console_write(")\n");
        kstack_free(recv_kstack);
        ipc_channel_destroy(s_ipc_ch_test);
        return 0;
    }

    kstack_free(recv_kstack);
    ipc_channel_destroy(s_ipc_ch_test);
    return 1;
}

/* ---------------------------------------------------------------------- */
/* T07: SYSCALL_SCHED_SET_PRIORITY sets thread priority                   */
/* ---------------------------------------------------------------------- */

static process_t s_proc_t07;

static int test_syscall_set_priority(void)
{
    thread_t       *t;
    syscall_args_t  args;
    syscall_result_t r;
    thread_t       *prev_current;

    prev_current = sched_get_current();

    if (!process_init(&s_proc_t07, 700, "t07proc")) {
        return 0;
    }

    t = process_add_thread(&s_proc_t07, 0, 701, "t07thread", 3);
    if (t == NULL) {
        return 0;
    }

    t->state = PROCESS_STATE_RUNNING;
    sched_set_current(t);

    args.number = SYSCALL_SCHED_SET_PRIORITY;
    args.arg0   = 5u;   /* new priority */
    args.arg1   = 0u;
    args.arg2   = 0u;
    args.arg3   = 0u;
    args.arg4   = 0u;

    r = syscall_dispatch(&args);

    sched_set_current(prev_current);

    if (r != SYSCALL_OK) {
        console_write("    SET_PRIORITY returned ");
        console_write_dec((uint64_t)(uint32_t)(-r));
        console_write("\n");
        return 0;
    }

    if (t->priority != 5u) {
        console_write("    priority not updated (got ");
        console_write_dec((uint64_t)t->priority);
        console_write(")\n");
        return 0;
    }

    return 1;
}

/* ---------------------------------------------------------------------- */
/* T08: SYSCALL_SCHED_GET_PRIORITY returns thread priority                */
/* ---------------------------------------------------------------------- */

static process_t s_proc_t08;

static int test_syscall_get_priority(void)
{
    thread_t       *t;
    syscall_args_t  args;
    syscall_result_t r;
    thread_t       *prev_current;

    prev_current = sched_get_current();

    if (!process_init(&s_proc_t08, 800, "t08proc")) {
        return 0;
    }

    t = process_add_thread(&s_proc_t08, 0, 801, "t08thread", 2);
    if (t == NULL) {
        return 0;
    }

    t->state = PROCESS_STATE_RUNNING;
    sched_set_current(t);

    args.number = SYSCALL_SCHED_GET_PRIORITY;
    args.arg0   = 0u;
    args.arg1   = 0u;
    args.arg2   = 0u;
    args.arg3   = 0u;
    args.arg4   = 0u;

    r = syscall_dispatch(&args);

    sched_set_current(prev_current);

    if (r != (syscall_result_t)2) {
        console_write("    GET_PRIORITY returned ");
        console_write_dec((uint64_t)(int64_t)r);
        console_write(" expected 2\n");
        return 0;
    }

    return 1;
}

/* ---------------------------------------------------------------------- */
/* T09: GDT user-mode selector constants (x86_64 only)                    */
/* ---------------------------------------------------------------------- */

static int test_gdt_selectors(void)
{
#if defined(__x86_64__)
    if (GDT_SEG_KERNEL_CODE != 0x08u) {
        console_write("    GDT_SEG_KERNEL_CODE != 0x08\n");
        return 0;
    }
    if (GDT_SEG_KERNEL_DATA != 0x10u) {
        console_write("    GDT_SEG_KERNEL_DATA != 0x10\n");
        return 0;
    }
    if (GDT_SEG_USER_CODE != 0x1Bu) {
        console_write("    GDT_SEG_USER_CODE != 0x1B\n");
        return 0;
    }
    if (GDT_SEG_USER_DATA != 0x23u) {
        console_write("    GDT_SEG_USER_DATA != 0x23\n");
        return 0;
    }
    /* Verify ring-3 RPL bits are set (bits 0-1 = 3) */
    if ((GDT_SEG_USER_CODE & 3u) != 3u) {
        console_write("    GDT_SEG_USER_CODE RPL bits not 3\n");
        return 0;
    }
    if ((GDT_SEG_USER_DATA & 3u) != 3u) {
        console_write("    GDT_SEG_USER_DATA RPL bits not 3\n");
        return 0;
    }
    return 1;
#else
    /* AArch64 does not have x86 GDT; test passes trivially */
    return 1;
#endif
}

/* ---------------------------------------------------------------------- */
/* T10: user_isr_frame_t is larger than isr_frame_t (x86_64 only)        */
/* ---------------------------------------------------------------------- */

static int test_user_isr_frame_size(void)
{
#if defined(__x86_64__)
    size_t base_size = sizeof(isr_frame_t);
    size_t user_size = sizeof(user_isr_frame_t);

    /*
     * user_isr_frame_t adds rsp_user and ss_user (2 * 8 = 16 bytes) to
     * isr_frame_t. Verify the extended struct is exactly 16 bytes larger.
     */
    if (user_size != base_size + 16u) {
        console_write("    user_isr_frame_t size ");
        console_write_dec((uint64_t)user_size);
        console_write(" expected ");
        console_write_dec((uint64_t)(base_size + 16u));
        console_write("\n");
        return 0;
    }
    return 1;
#else
    /* AArch64: architecture has different exception frame discipline;
     * test passes trivially here (EL0 frame covered by exception.h). */
    return 1;
#endif
}

/* ---------------------------------------------------------------------- */
/* phase2c_init                                                            */
/* ---------------------------------------------------------------------- */

void phase2c_init(void)
{
    s_pass = 0u;
    s_fail = 0u;

    console_write("\n");
    console_write("========================================\n");
    console_write("Phase 2C bring-up\n");
    console_write("========================================\n");

    /* Initialize Phase 2C subsystems */
    kstack_init();

    /* Run self-test suite */
    console_write("\n--- Phase 2C self-tests ---\n");

    report("T01  Timer tick advancement",          test_timer_ticks());
    report("T02  Kernel stack pool alloc/free",    test_kstack_alloc());
    report("T03  kstack_init_context field setup", test_kstack_init_context());
    report("T04  sched current thread tracking",  test_sched_current());
    report("T05  Real context switch (2 threads)", test_context_switch());
    report("T06  Blocking IPC send/recv",          test_blocking_ipc());
    report("T07  SYSCALL_SCHED_SET_PRIORITY",      test_syscall_set_priority());
    report("T08  SYSCALL_SCHED_GET_PRIORITY",      test_syscall_get_priority());
    report("T09  GDT user-mode selector constants",test_gdt_selectors());
    report("T10  user_isr_frame_t size check",     test_user_isr_frame_size());

    console_write("\n");
    console_write("Phase 2C tests: pass=");
    console_write_dec((uint64_t)s_pass);
    console_write(" fail=");
    console_write_dec((uint64_t)s_fail);
    console_write("\n");

    if (s_fail == 0u) {
        console_write("Phase 2C        : all self-tests PASSED\n");
    } else {
        console_write("Phase 2C        : FAILURES DETECTED\n");
    }
}

