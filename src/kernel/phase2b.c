/*
 * Zapada - src/kernel/phase2b.c
 *
 * Phase 2B subsystem initialization and self-test orchestration.
 *
 * Sequence:
 *   1. Process subsystem: create kernel idle process.
 *   2. Scheduler init: run queue and timer.
 *   3. Syscall dispatch table init.
 *   4. IPC channel pool init.
 *   5. Self-test: process lifecycle state transitions.
 *   6. Self-test: scheduler enqueue / dequeue / preemption tick path.
 *   7. Self-test: IPC send / receive / full / empty / type-filter paths.
 *   8. Self-test: syscall dispatch for known and unknown numbers.
 *
 * All output uses console_write for cross-arch parity (x86_64 serial /
 * AArch64 UART both funnel through console.h).
 */

#include <kernel/phase2b.h>
#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <kernel/sched/timer.h>
#include <kernel/syscall/syscall.h>
#include <kernel/ipc/ipc.h>
#include <kernel/console.h>
#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Static kernel idle process (no heap needed)                            */
/* ---------------------------------------------------------------------- */

static process_t s_kernel_process;

/* ---------------------------------------------------------------------- */
/* Self-test helpers                                                       */
/* ---------------------------------------------------------------------- */

static uint32_t s_test_pass;
static uint32_t s_test_fail;

static void test_check(const char *label, bool condition)
{
    if (condition) {
        console_write("  [PASS] ");
        s_test_pass++;
    } else {
        console_write("  [FAIL] ");
        s_test_fail++;
    }
    console_write(label);
    console_write("\n");
}

/* ---------------------------------------------------------------------- */
/* Self-test: process lifecycle                                            */
/* ---------------------------------------------------------------------- */

static void test_process_lifecycle(void)
{
    process_t  p;
    thread_t  *t;
    bool       ok;

    console_write("\nPhase 2B: process lifecycle test\n");

    ok = process_init(&p, 100, "test_proc");
    test_check("process_init succeeds", ok);
    test_check("process in INIT state", p.state == PROCESS_STATE_INIT);

    t = process_add_thread(&p, 0, 1, "thread_0", 2);
    test_check("process_add_thread returns non-NULL", t != NULL);
    test_check("thread in INIT state", t != NULL && t->state == PROCESS_STATE_INIT);
    test_check("thread count is 1", p.thread_count == 1u);

    ok = process_ready(&p);
    test_check("process_ready succeeds", ok);
    test_check("process in READY state", p.state == PROCESS_STATE_READY);
    test_check("thread in READY state after process_ready",
               t != NULL && t->state == PROCESS_STATE_READY);

    ok = process_exit(&p, 42);
    test_check("process_exit succeeds", ok);
    test_check("process in ZOMBIE state", p.state == PROCESS_STATE_ZOMBIE);
    test_check("exit code recorded", p.exit_code == 42);
    test_check("thread transitions to ZOMBIE on exit",
               t != NULL && t->state == PROCESS_STATE_ZOMBIE);

    /* Error cases */
    ok = process_init(NULL, 0, "null_proc");
    test_check("process_init rejects NULL pointer", !ok);

    ok = process_ready(NULL);
    test_check("process_ready rejects NULL pointer", !ok);
}

/* ---------------------------------------------------------------------- */
/* Self-test: scheduler                                                    */
/* ---------------------------------------------------------------------- */

static void test_scheduler(void)
{
    process_t  a_proc;
    process_t  b_proc;
    thread_t  *ta;
    thread_t  *tb;
    thread_t  *next;
    bool       ok;

    console_write("\nPhase 2B: scheduler test\n");

    /* Create two READY threads at different priorities */
    process_init(&a_proc, 10, "proc_a");
    ta = process_add_thread(&a_proc, 0, 10, "thread_a", 0); /* prio 0 = highest */
    process_ready(&a_proc);

    process_init(&b_proc, 11, "proc_b");
    tb = process_add_thread(&b_proc, 0, 11, "thread_b", 1); /* prio 1 */
    process_ready(&b_proc);

    test_check("threads created for scheduler test", ta != NULL && tb != NULL);

    /* Enqueue both threads */
    ok = sched_enqueue(ta);
    test_check("enqueue high-priority thread succeeds", ok);
    ok = sched_enqueue(tb);
    test_check("enqueue low-priority thread succeeds", ok);

    test_check("ready count is 2", sched_get_ready_count() == 2u);

    /* sched_next() should return the higher-priority thread */
    next = sched_next(NULL);
    test_check("sched_next returns highest priority thread",
               next != NULL && next->tid == ta->tid);
    test_check("ready count is 1 after dequeue", sched_get_ready_count() == 1u);

    /* Simulate the thread running, then tick-expiry preemption */
    next->state           = PROCESS_STATE_RUNNING;
    next->ticks_remaining = 1;
    sched_tick(next);
    test_check("tick decrements ticks_remaining to 0",
               next->ticks_remaining == 0u);
    test_check("scheduler state is PREEMPT after tick expiry",
               sched_get_state() == SCHED_PREEMPT);

    /* sched_next requeues the running thread due to PREEMPT flag */
    thread_t *after_preempt = sched_next(next);
    test_check("sched_next after preempt returns highest-priority runnable thread",
               after_preempt != NULL && after_preempt->tid == ta->tid);
    test_check("duplicate enqueue is rejected for already-queued thread",
               !sched_enqueue(tb));

    /* sched_yield */
    if (after_preempt != NULL) {
        after_preempt->state = PROCESS_STATE_RUNNING;
        ok = sched_yield(after_preempt);
        test_check("sched_yield succeeds", ok);
        test_check("thread back in READY state after yield",
                   after_preempt->state == PROCESS_STATE_READY);
    }

    /* sched_block / sched_unblock - use a fresh process/thread */
    {
        process_t  c_proc;
        thread_t  *tc;

        process_init(&c_proc, 12, "proc_c");
        tc = process_add_thread(&c_proc, 0, 12, "thread_c", 3);
        process_ready(&c_proc);

        if (tc != NULL) {
            ok = sched_enqueue(tc);
            test_check("enqueue thread_c for block test", ok);

            ok = sched_block(tc);
            test_check("sched_block succeeds", ok);
            test_check("thread in BLOCKED state and removed from ready queue",
                       tc->state == PROCESS_STATE_BLOCKED && sched_get_ready_count() == 2u);

            ok = sched_unblock(tc);
            test_check("sched_unblock succeeds", ok);
            test_check("thread back in READY state after unblock",
                       tc->state == PROCESS_STATE_READY && sched_get_ready_count() == 3u);

            /* Drain tc from queue so print_queues is bounded */
            (void)sched_dequeue();
        }
    }

    /* Drain remaining threads from queues before print */
    while (sched_get_ready_count() > 0u) {
        (void)sched_dequeue();
    }

    /* Dump queues to serial for visual confirmation */
    sched_print_queues();
}

/* ---------------------------------------------------------------------- */
/* Self-test: IPC channels                                                */
/* ---------------------------------------------------------------------- */

static void test_ipc(void)
{
    ipc_handle_t   ch;
    ipc_result_t   r;
    ipc_message_t  msg;
    ipc_message_t  out;
    uint32_t       i;

    console_write("\nPhase 2B: IPC channel test\n");

    /* Create a channel */
    ch = ipc_channel_create();
    test_check("ipc_channel_create returns valid handle", ch != IPC_HANDLE_INVALID);

    /* Send a message */
    msg.type        = IPC_MSG_TYPE_SIGNAL;
    msg.payload_len = 0;
    r = ipc_trysend(ch, &msg);
    test_check("ipc_trysend succeeds on empty channel", r == IPC_OK);

    test_check("channel length is 1 after send", ipc_channel_len(ch) == 1u);

    /* Receive with type ANY */
    r = ipc_tryrecv(ch, IPC_MSG_TYPE_ANY, &out);
    test_check("ipc_tryrecv succeeds on non-empty channel", r == IPC_OK);
    test_check("received message type matches sent", out.type == IPC_MSG_TYPE_SIGNAL);
    test_check("channel length is 0 after receive", ipc_channel_len(ch) == 0u);

    /* Receive from empty channel returns IPC_ERR_EMPTY */
    r = ipc_tryrecv(ch, IPC_MSG_TYPE_ANY, &out);
    test_check("ipc_tryrecv on empty channel returns IPC_ERR_EMPTY",
               r == IPC_ERR_EMPTY);

    /* Fill channel to capacity */
    msg.type        = IPC_MSG_TYPE_DATA;
    msg.payload_len = 8u;
    msg.payload[0]  = 0xDEADBEEFCAFEBABEull;

    for (i = 0; i < IPC_CHANNEL_CAPACITY; i++) {
        r = ipc_trysend(ch, &msg);
    }
    test_check("channel accepts exactly IPC_CHANNEL_CAPACITY messages",
               ipc_channel_len(ch) == IPC_CHANNEL_CAPACITY);

    /* One more send should fail with FULL */
    r = ipc_trysend(ch, &msg);
    test_check("ipc_trysend on full channel returns IPC_ERR_FULL",
               r == IPC_ERR_FULL);

    /* Drain and check payload integrity */
    r = ipc_tryrecv(ch, IPC_MSG_TYPE_DATA, &out);
    test_check("ipc_tryrecv with type filter DATA succeeds",
               r == IPC_OK);
    test_check("payload data preserved across send/receive",
               out.payload[0] == 0xDEADBEEFCAFEBABEull);

    /* Type mismatch test: send DATA, receive expecting SIGNAL */
    {
        ipc_message_t  type_msg;
        type_msg.type        = IPC_MSG_TYPE_DATA;
        type_msg.payload_len = 0;

        /* Clear channel first */
        while (ipc_channel_len(ch) > 0u) {
            ipc_tryrecv(ch, IPC_MSG_TYPE_ANY, &out);
        }

        r = ipc_trysend(ch, &type_msg);
        test_check("send DATA message for type-filter test", r == IPC_OK);

        r = ipc_tryrecv(ch, IPC_MSG_TYPE_SIGNAL, &out);
        test_check("receive with SIGNAL filter on DATA message returns IPC_ERR_TYPE",
                   r == IPC_ERR_TYPE);

        /* The message must still be in the channel */
        test_check("message not consumed after type mismatch",
                   ipc_channel_len(ch) == 1u);
    }

    /*
     * Phase 2C+: blocking IPC is now implemented.
     * In this Phase 2B compatibility test we intentionally exercise the
     * non-blocking-fast-path behavior of the public blocking calls:
     *   - channel currently contains exactly one message (from the type-mismatch test)
     *   - ipc_send() should succeed immediately because the channel is not full
     *   - ipc_recv() should then succeed immediately because the channel is non-empty
     * This preserves the 58-test count while aligning the assertions with the
     * real IPC contract implemented in Phase 2C.
     */
    r = ipc_send(ch, &msg);
    test_check("ipc_send succeeds immediately when channel is not full", r == IPC_OK);

    r = ipc_recv(ch, IPC_MSG_TYPE_ANY, &out);
    test_check("ipc_recv succeeds immediately when channel is non-empty", r == IPC_OK);

    /* Invalid handle tests */
    r = ipc_trysend(IPC_HANDLE_INVALID, &msg);
    test_check("ipc_trysend with invalid handle returns IPC_ERR_INVAL",
               r == IPC_ERR_INVAL);

    r = ipc_tryrecv(IPC_HANDLE_INVALID, IPC_MSG_TYPE_ANY, &out);
    test_check("ipc_tryrecv with invalid handle returns IPC_ERR_INVAL",
               r == IPC_ERR_INVAL);

    /* Destroy channel */
    r = ipc_channel_destroy(ch);
    test_check("ipc_channel_destroy succeeds", r == IPC_OK);

    r = ipc_trysend(ch, &msg);
    test_check("ipc_trysend on destroyed channel returns IPC_ERR_INVAL or CLOSED",
               r == IPC_ERR_INVAL || r == IPC_ERR_CLOSED);

    /* Destroy invalid handle */
    r = ipc_channel_destroy(IPC_HANDLE_INVALID);
    test_check("ipc_channel_destroy with invalid handle returns IPC_ERR_INVAL",
               r == IPC_ERR_INVAL);
}

/* ---------------------------------------------------------------------- */
/* Self-test: syscall dispatch                                             */
/* ---------------------------------------------------------------------- */

static void test_syscall(void)
{
    syscall_args_t   args;
    syscall_result_t r;
    ipc_handle_t     ch;
    ipc_message_t    msg;
    ipc_message_t    out;

    console_write("\nPhase 2B: syscall dispatch test\n");

    args.number = SYSCALL_GET_PID;
    args.arg0 = args.arg1 = args.arg2 = args.arg3 = args.arg4 = 0;
    r = syscall_dispatch(&args);
    test_check("SYSCALL_GET_PID dispatches and returns PID",
               r == (syscall_result_t)PID_KERNEL);

    ch = ipc_channel_create();
    msg.type = IPC_MSG_TYPE_SIGNAL;
    msg.payload_len = 0u;

    args.number = SYSCALL_IPC_TRYSEND;
    args.arg0 = (uint64_t)ch;
    args.arg1 = (uint64_t)(uintptr_t)&msg;
    args.arg2 = args.arg3 = args.arg4 = 0;
    r = syscall_dispatch(&args);
    test_check("SYSCALL_IPC_TRYSEND dispatches and returns OK", r == SYSCALL_OK);

    args.number = SYSCALL_IPC_TRYRECV;
    args.arg0 = (uint64_t)ch;
    args.arg1 = (uint64_t)IPC_MSG_TYPE_ANY;
    args.arg2 = (uint64_t)(uintptr_t)&out;
    r = syscall_dispatch(&args);
    test_check("SYSCALL_IPC_TRYRECV dispatches and returns OK",
               r == SYSCALL_OK && out.type == IPC_MSG_TYPE_SIGNAL);

    (void)ipc_channel_destroy(ch);

    args.number = SYSCALL_DIAG_DUMP_SCHED;
    args.arg0 = args.arg1 = args.arg2 = args.arg3 = args.arg4 = 0;
    r = syscall_dispatch(&args);
    test_check("SYSCALL_DIAG_DUMP_SCHED dispatches and returns OK", r == SYSCALL_OK);

    /* Unknown syscall */
    args.number = 0x9999u;
    r = syscall_dispatch(&args);
    test_check("Unknown syscall returns SYSCALL_ERR_NOSYS", r == SYSCALL_ERR_NOSYS);

    /* NULL args */
    r = syscall_dispatch(NULL);
    test_check("syscall_dispatch with NULL args returns SYSCALL_ERR_INVAL",
               r == SYSCALL_ERR_INVAL);
}

/* ---------------------------------------------------------------------- */
/* phase2b_init                                                            */
/* ---------------------------------------------------------------------- */

bool phase2b_init(void)
{
    bool all_pass;

    console_write("\n");
    console_write("Phase 2B        : initializing subsystems\n");
    console_write("---------------------------------------------\n");

    /* 1. Process subsystem: create kernel idle process */
    if (!process_init(&s_kernel_process, PID_KERNEL, "kernel")) {
        console_write("Phase 2B        : ERROR: kernel process init failed\n");
        return false;
    }
    if (process_add_thread(&s_kernel_process, 0, 1, "idle", 7 /* lowest prio */) == NULL) {
        console_write("Phase 2B        : ERROR: idle thread creation failed\n");
        return false;
    }
    if (!process_ready(&s_kernel_process)) {
        console_write("Phase 2B        : ERROR: kernel process ready failed\n");
        return false;
    }
    console_write("Process         : kernel process created (pid=1, idle thread)\n");

    /* 2. Scheduler */
    sched_init();

    /* 3. Timer stub (100 Hz target) */
    timer_init(100);

    /* 4. Syscall dispatch table */
    syscall_init();

    /* 5. IPC channel pool */
    ipc_init();

    console_write("\n");
    console_write("Phase 2B        : running self-tests\n");
    console_write("---------------------------------------------\n");

    s_test_pass = 0;
    s_test_fail = 0;

    test_process_lifecycle();
    test_scheduler();
    test_ipc();
    test_syscall();

    console_write("\n");
    console_write("Phase 2B tests  : pass=");
    console_write_dec((uint64_t)s_test_pass);
    console_write(" fail=");
    console_write_dec((uint64_t)s_test_fail);
    console_write("\n");

    all_pass = (s_test_fail == 0u);

    if (all_pass) {
        console_write("Phase 2B        : all self-tests passed\n");
    } else {
        console_write("Phase 2B        : SELF-TEST FAILURES DETECTED\n");
    }

    console_write("Phase 2B complete.\n");

    return all_pass;
}

