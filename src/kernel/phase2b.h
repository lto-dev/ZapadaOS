/*
 * Zapada - src/kernel/phase2b.h
 *
 * Phase 2B bootstrap orchestration header.
 *
 * phase2b_init() initializes all Phase 2B subsystems in dependency order:
 *   1. Process subsystem (kernel idle process)
 *   2. Scheduler (run queues)
 *   3. Timer stub
 *   4. Syscall dispatch table
 *   5. IPC channel pool
 *   6. Self-test: scheduler dispatch path
 *   7. Self-test: IPC send/receive/failure paths
 *   8. Self-test: syscall dispatch table
 *
 * All self-tests produce deterministic serial output for gate verification.
 * Returns true if all subsystem initializations and self-tests pass.
 */

#ifndef ZAPADA_PHASE2B_H
#define ZAPADA_PHASE2B_H

#include <kernel/types.h>

/*
 * phase2b_init - initialize Phase 2B subsystems and run self-tests.
 *
 * Called from kernel_main (x86_64) and kernel_main_aarch64 (AArch64) after
 * Phase 2A managed runtime completes.
 *
 * Returns true if all subsystems initialized and all self-tests pass.
 */
bool phase2b_init(void);

#endif /* ZAPADA_PHASE2B_H */


