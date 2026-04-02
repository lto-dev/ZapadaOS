/*
 * Zapada - src/kernel/phase32_step1.h
 *
 * Phase 3.2 Step 1 self-tests.
 *
 * Tests 10 interpreter gap fixes introduced in Phase 3.2:
 *
 *   T01  Gap 1 (newarr ref elem_sz):  gc_alloc_array with elem_sz=8, count=4
 *        -- read count from array header = 4
 *   T02  Gap 1 (newarr ref elem_sz):  8-byte stride in ref-type array
 *        -- write sentinel pointer to slot 1, read back matches
 *   T03  Gap 2 (bge.un.s):  branch taken when 5u >= 3u
 *   T04  Gap 2 (blt.un.s):  branch NOT taken when 5u < 3u is false
 *   T05  Gap 3 (shr.un):  0x80000000u >> 3 = 0x10000000 (no sign extension)
 *   T06  Gap 4 (switch):  index=1, 3 arms, arm 1 taken
 *   T07  Gap 4 (switch):  index=5 out-of-range 3 arms, falls through
 *   T08  Gap 7 (div.un):  7u / 2u = 3
 *   T09  Gap 7 (rem.un):  7u % 2u = 1
 *   T10  Gap 8 (nop):     three nops followed by ret -- no crash
 *
 * Gate emitted when all 10 tests pass: [Gate] Phase3.2-S1
 */

#ifndef ZAPADA_PHASE32_STEP1_H
#define ZAPADA_PHASE32_STEP1_H

/*
 * phase32_step1_run - execute all 10 Phase 3.2 Step 1 self-tests.
 *
 * Writes per-test [PASS]/[FAIL] lines to the console.
 * Emits "[Gate] Phase3.2-S1" when all tests pass.
 */
void phase32_step1_run(void);

#endif /* ZAPADA_PHASE32_STEP1_H */


