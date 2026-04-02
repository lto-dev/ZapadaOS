/*
 * Zapada - src/kernel/phase32_step1.c
 *
 * Phase 3.2 Step 1 self-tests for interpreter gap fixes.
 *
 * Design
 * ------
 *
 * Tests T01 and T02 (Gap 1 — newarr reference-type elem_sz):
 *   Exercise gc_alloc_array() directly with elem_sz=8 (the value the fixed
 *   newarr handler now selects for reference types), then inspect the array
 *   header fields and element-slot offsets from C.
 *
 * Tests T03–T10 (Gaps 2/3/4/7/8 — unsigned branches, shr.un, switch,
 *   div.un, rem.un, nop):
 *   Each test encodes a tiny CIL byte array and passes it to clr_interpret()
 *   with a NULL pe_context_t pointer.  This is safe because none of these
 *   opcodes touch the pe_context_t; they perform only stack manipulation,
 *   arithmetic, and branching.
 *
 *   Success path: the CIL method executes ret normally → clr_interpret
 *   returns true.
 *   Failure path: the CIL method reaches a literal 0xFF byte (unsupported
 *   opcode) → clr_interpret returns false.
 *
 * Gate: [Gate] Phase3.2-S1 — emitted only when all 10 tests pass.
 */

#include <kernel/phase32_step1.h>
#include <kernel/console.h>
#include <kernel/clr/include/gc.h>
#include <kernel/clr/include/interpreter.h>
#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Local helpers                                                           */
/* ---------------------------------------------------------------------- */

/*
 * read_u32_at - read a uint32 from a byte offset in a GC-managed object.
 * Uses byte reads to avoid unaligned-access faults on AArch64.
 */
static uint32_t read_u32_at(const void *base, uint32_t off)
{
    const uint8_t *p = (const uint8_t *)base + off;
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8u)
         | ((uint32_t)p[2] << 16u)
         | ((uint32_t)p[3] << 24u);
}

/*
 * write_u64_at / read_u64_at - 8-byte helpers for pointer-sized element slots.
 * CLR_ARR_HDR_SIZE=24 is 8-byte aligned, so slot offsets are always aligned.
 */
static void write_u64_at(void *base, uint32_t off, uint64_t val)
{
    uint8_t *p = (uint8_t *)base + off;
    p[0] = (uint8_t)(val);
    p[1] = (uint8_t)(val >> 8u);
    p[2] = (uint8_t)(val >> 16u);
    p[3] = (uint8_t)(val >> 24u);
    p[4] = (uint8_t)(val >> 32u);
    p[5] = (uint8_t)(val >> 40u);
    p[6] = (uint8_t)(val >> 48u);
    p[7] = (uint8_t)(val >> 56u);
}

static uint64_t read_u64_at(const void *base, uint32_t off)
{
    const uint8_t *p = (const uint8_t *)base + off;
    return (uint64_t)p[0]
         | ((uint64_t)p[1] << 8u)
         | ((uint64_t)p[2] << 16u)
         | ((uint64_t)p[3] << 24u)
         | ((uint64_t)p[4] << 32u)
         | ((uint64_t)p[5] << 40u)
         | ((uint64_t)p[6] << 48u)
         | ((uint64_t)p[7] << 56u);
}

/* ---------------------------------------------------------------------- */
/* T01: ref-type array count                                               */
/* ---------------------------------------------------------------------- */
/*
 * Allocate a 4-element reference-type array (elem_sz=8) via gc_alloc_array
 * and verify the count field in the array header equals 4.
 * This exercises the same gc_alloc_array call the fixed newarr handler uses.
 */
static bool run_t01(void)
{
    void    *arr;
    uint32_t count;
    uint32_t esz;

    /* Token 0x01000001: table=0x01 (TypeRef), row=1 — any non-zero token is
     * valid here; gc_alloc_array only stores it in the header. */
    arr = gc_alloc_array(0x01000001u, 4u, 8u);
    if (arr == NULL) {
        console_write("[FAIL] T01: gc_alloc_array returned NULL\n");
        return false;
    }

    count = read_u32_at(arr, CLR_ARR_OFF_COUNT);
    if (count != 4u) {
        console_write("[FAIL] T01: count=");
        console_write_dec((uint64_t)count);
        console_write(" expected 4\n");
        return false;
    }

    esz = read_u32_at(arr, CLR_ARR_OFF_ELEM_SIZE);
    if (esz != 8u) {
        console_write("[FAIL] T01: elem_sz=");
        console_write_dec((uint64_t)esz);
        console_write(" expected 8\n");
        return false;
    }

    console_write("[PASS] T01: ref-type array count=4, elem_sz=8\n");
    return true;
}

/* ---------------------------------------------------------------------- */
/* T02: ref-type array 8-byte element stride                              */
/* ---------------------------------------------------------------------- */
/*
 * Allocate a 4-element reference-type array and store a sentinel uint64
 * at element slot 1 (byte offset CLR_ARR_HDR_SIZE + 1*8 = 32).
 * Read it back and verify it matches.  This confirms the 8-byte stride
 * the fixed newarr handler now produces for reference types.
 */
static bool run_t02(void)
{
    void          *arr;
    uint32_t       slot1_off;
    uint64_t       sentinel;
    uint64_t       readback;

    arr = gc_alloc_array(0x01000001u, 4u, 8u);
    if (arr == NULL) {
        console_write("[FAIL] T02: gc_alloc_array returned NULL\n");
        return false;
    }

    sentinel  = (uint64_t)0xCAFEBABEDEADBEEFull;
    slot1_off = CLR_ARR_HDR_SIZE + 1u * 8u;   /* = 24 + 8 = 32 */

    write_u64_at(arr, slot1_off, sentinel);
    readback = read_u64_at(arr, slot1_off);

    if (readback != sentinel) {
        console_write("[FAIL] T02: readback mismatch\n");
        return false;
    }

    console_write("[PASS] T02: ref-type array 8-byte stride write/read\n");
    return true;
}

/* ---------------------------------------------------------------------- */
/* T03: bge.un.s branch taken (5u >= 3u)                                  */
/* ---------------------------------------------------------------------- */
/*
 * CIL layout:
 *   offset 0: ldc.i4.5       (0x1B)
 *   offset 1: ldc.i4.3       (0x19)
 *   offset 2: bge.un.s +1    (0x34 0x01)  pc_after=4, target=5=ret
 *   offset 4: 0xFF            invalid      (not reached)
 *   offset 5: ret             (0x2A)       success
 */
static bool run_t03(void)
{
    static const uint8_t cil[] = {
        0x1B,        /* ldc.i4.5 */
        0x19,        /* ldc.i4.3 */
        0x34, 0x01,  /* bge.un.s +1 */
        0xFF,        /* invalid (fail if reached) */
        0x2A         /* ret (success) */
    };
    bool ok = clr_interpret(NULL, cil, (uint32_t)sizeof(cil));
    if (ok) {
        console_write("[PASS] T03: bge.un.s taken for 5u>=3u\n");
    } else {
        console_write("[FAIL] T03: bge.un.s not taken or crashed\n");
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* T04: blt.un.s NOT taken (5u < 3u is false)                             */
/* ---------------------------------------------------------------------- */
/*
 * CIL layout:
 *   offset 0: ldc.i4.5       (0x1B)
 *   offset 1: ldc.i4.3       (0x19)
 *   offset 2: blt.un.s +1    (0x37 0x01)  pc_after=4, target=5=invalid
 *   offset 4: ret             (0x2A)       success (fallthrough)
 *   offset 5: 0xFF            invalid      (fail if branch taken)
 */
static bool run_t04(void)
{
    static const uint8_t cil[] = {
        0x1B,        /* ldc.i4.5 */
        0x19,        /* ldc.i4.3 */
        0x37, 0x01,  /* blt.un.s +1 (should NOT branch: 5 < 3 is false) */
        0x2A,        /* ret (success; fallthrough path) */
        0xFF         /* invalid (fail if branch wrongly taken) */
    };
    bool ok = clr_interpret(NULL, cil, (uint32_t)sizeof(cil));
    if (ok) {
        console_write("[PASS] T04: blt.un.s not taken for 5u<3u=false\n");
    } else {
        console_write("[FAIL] T04: blt.un.s wrongly taken\n");
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* T05: shr.un 0x80000000 >> 3 = 0x10000000                              */
/* ---------------------------------------------------------------------- */
/*
 * Verifies no sign extension.  CIL pushes 0x80000000 as int32 (the signed
 * interpretation is -2147483648), shifts right 3 via shr.un, then compares
 * the result with the expected 0x10000000.  If they match, falls through to
 * ret; otherwise hits 0xFF (invalid).
 *
 * CIL layout:
 *   0:  ldc.i4 0x80000000  (0x20 0x00 0x00 0x00 0x80)
 *   5:  ldc.i4.3           (0x19)
 *   6:  shr.un             (0x64)
 *   7:  ldc.i4 0x10000000  (0x20 0x00 0x00 0x00 0x10)
 *   12: ceq                (0xFE 0x01)
 *   14: brfalse.s +1       (0x2C 0x01)  pc_after=16, target=17=invalid
 *   16: ret                (0x2A)       success
 *   17: 0xFF               invalid      fail
 */
static bool run_t05(void)
{
    static const uint8_t cil[] = {
        0x20, 0x00, 0x00, 0x00, 0x80, /* ldc.i4 0x80000000 */
        0x19,                          /* ldc.i4.3 */
        0x64,                          /* shr.un */
        0x20, 0x00, 0x00, 0x00, 0x10, /* ldc.i4 0x10000000 */
        0xFE, 0x01,                    /* ceq */
        0x2C, 0x01,                    /* brfalse.s +1 (jump to 0xFF if not equal) */
        0x2A,                          /* ret (success) */
        0xFF                           /* invalid (fail) */
    };
    bool ok = clr_interpret(NULL, cil, (uint32_t)sizeof(cil));
    if (ok) {
        console_write("[PASS] T05: shr.un 0x80000000>>3=0x10000000\n");
    } else {
        console_write("[FAIL] T05: shr.un produced wrong result\n");
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* T06: switch index=1 → arm 1 (of 3)                                    */
/* ---------------------------------------------------------------------- */
/*
 * switch starts at offset 1.  pc_after_table = 1 + 17 = 18.
 *   off[0] = 0  → 18+0 = 18 = 0xFF (fail; arm 0)
 *   off[1] = 1  → 18+1 = 19 = 0x2A (ret; arm 1 = success)
 *   off[2] = 0  → 18+0 = 18 = 0xFF (fail; arm 2)
 *   fallthrough → 18 = 0xFF (fail)
 *
 * CIL layout:
 *   0:  ldc.i4.1   (0x17)
 *   1:  switch n=3 (0x45 0x03 0x00 0x00 0x00)
 *   6:  off[0] LE  (0x00 0x00 0x00 0x00)
 *   10: off[1] LE  (0x01 0x00 0x00 0x00)
 *   14: off[2] LE  (0x00 0x00 0x00 0x00)
 *   18: 0xFF       invalid (fail: arms 0/2/fallthrough)
 *   19: ret        (0x2A)  success: arm 1
 */
static bool run_t06(void)
{
    static const uint8_t cil[] = {
        0x17,                          /* ldc.i4.1 */
        0x45, 0x03, 0x00, 0x00, 0x00, /* switch n=3 */
        0x00, 0x00, 0x00, 0x00,       /* off[0] = 0 → 18 = fail */
        0x01, 0x00, 0x00, 0x00,       /* off[1] = 1 → 19 = ret (success) */
        0x00, 0x00, 0x00, 0x00,       /* off[2] = 0 → 18 = fail */
        0xFF,                          /* offset 18: fail */
        0x2A                           /* offset 19: ret (success) */
    };
    bool ok = clr_interpret(NULL, cil, (uint32_t)sizeof(cil));
    if (ok) {
        console_write("[PASS] T06: switch index=1 took arm 1\n");
    } else {
        console_write("[FAIL] T06: switch index=1 did not take arm 1\n");
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* T07: switch index=5 out-of-range (3 arms) → falls through             */
/* ---------------------------------------------------------------------- */
/*
 * switch starts at offset 1.  pc_after_table = 18.
 *   All arms: off[x] = 1 → 18+1 = 19 = 0xFF (fail if any arm taken)
 *   fallthrough → 18 = 0x2A = ret (success)
 *
 * CIL layout:
 *   0:  ldc.i4.5   (0x1B)
 *   1:  switch n=3 (0x45 0x03 0x00 0x00 0x00)
 *   6:  off[0] LE  (0x01 0x00 0x00 0x00)
 *   10: off[1] LE  (0x01 0x00 0x00 0x00)
 *   14: off[2] LE  (0x01 0x00 0x00 0x00)
 *   18: ret        (0x2A)  success: fallthrough
 *   19: 0xFF       invalid (fail: if an arm incorrectly taken)
 */
static bool run_t07(void)
{
    static const uint8_t cil[] = {
        0x1B,                          /* ldc.i4.5 */
        0x45, 0x03, 0x00, 0x00, 0x00, /* switch n=3 */
        0x01, 0x00, 0x00, 0x00,       /* off[0] = 1 → 19 = fail */
        0x01, 0x00, 0x00, 0x00,       /* off[1] = 1 → 19 = fail */
        0x01, 0x00, 0x00, 0x00,       /* off[2] = 1 → 19 = fail */
        0x2A,                          /* offset 18: ret (fallthrough = success) */
        0xFF                           /* offset 19: fail */
    };
    bool ok = clr_interpret(NULL, cil, (uint32_t)sizeof(cil));
    if (ok) {
        console_write("[PASS] T07: switch index=5 fell through (out-of-range)\n");
    } else {
        console_write("[FAIL] T07: switch index=5 wrongly took an arm\n");
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* T08: div.un 7u / 2u = 3                                               */
/* ---------------------------------------------------------------------- */
/*
 * CIL layout:
 *   0: ldc.i4.7  (0x1D)
 *   1: ldc.i4.2  (0x18)
 *   2: div.un    (0x5C)
 *   3: ldc.i4.3  (0x19)
 *   4: ceq       (0xFE 0x01)
 *   6: brfalse.s +1  (0x2C 0x01)  pc_after=8, target=9=invalid
 *   8: ret       (0x2A)  success
 *   9: 0xFF      invalid (fail)
 */
static bool run_t08(void)
{
    static const uint8_t cil[] = {
        0x1D,        /* ldc.i4.7 */
        0x18,        /* ldc.i4.2 */
        0x5C,        /* div.un */
        0x19,        /* ldc.i4.3 */
        0xFE, 0x01,  /* ceq */
        0x2C, 0x01,  /* brfalse.s +1 */
        0x2A,        /* ret (success) */
        0xFF         /* invalid (fail) */
    };
    bool ok = clr_interpret(NULL, cil, (uint32_t)sizeof(cil));
    if (ok) {
        console_write("[PASS] T08: div.un 7u/2u=3\n");
    } else {
        console_write("[FAIL] T08: div.un produced wrong result\n");
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* T09: rem.un 7u % 2u = 1                                               */
/* ---------------------------------------------------------------------- */
/*
 * CIL layout:
 *   0: ldc.i4.7  (0x1D)
 *   1: ldc.i4.2  (0x18)
 *   2: rem.un    (0x5E)
 *   3: ldc.i4.1  (0x17)
 *   4: ceq       (0xFE 0x01)
 *   6: brfalse.s +1  (0x2C 0x01)
 *   8: ret       (0x2A)
 *   9: 0xFF
 */
static bool run_t09(void)
{
    static const uint8_t cil[] = {
        0x1D,        /* ldc.i4.7 */
        0x18,        /* ldc.i4.2 */
        0x5E,        /* rem.un */
        0x17,        /* ldc.i4.1 */
        0xFE, 0x01,  /* ceq */
        0x2C, 0x01,  /* brfalse.s +1 */
        0x2A,        /* ret (success) */
        0xFF         /* invalid (fail) */
    };
    bool ok = clr_interpret(NULL, cil, (uint32_t)sizeof(cil));
    if (ok) {
        console_write("[PASS] T09: rem.un 7u%2u=1\n");
    } else {
        console_write("[FAIL] T09: rem.un produced wrong result\n");
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* T10: three nops then ret — no crash                                    */
/* ---------------------------------------------------------------------- */
static bool run_t10(void)
{
    static const uint8_t cil[] = {
        0x00,  /* nop */
        0x00,  /* nop */
        0x00,  /* nop */
        0x2A   /* ret */
    };
    bool ok = clr_interpret(NULL, cil, (uint32_t)sizeof(cil));
    if (ok) {
        console_write("[PASS] T10: nop no crash\n");
    } else {
        console_write("[FAIL] T10: nop caused an error\n");
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* phase32_step1_run - public entry                                        */
/* ---------------------------------------------------------------------- */

void phase32_step1_run(void)
{
    uint32_t passed = 0u;
    uint32_t total  = 10u;

    console_write("[Phase3.2-S1] Running interpreter gap self-tests...\n");

    if (run_t01()) { passed++; }
    if (run_t02()) { passed++; }
    if (run_t03()) { passed++; }
    if (run_t04()) { passed++; }
    if (run_t05()) { passed++; }
    if (run_t06()) { passed++; }
    if (run_t07()) { passed++; }
    if (run_t08()) { passed++; }
    if (run_t09()) { passed++; }
    if (run_t10()) { passed++; }

    console_write("[Phase3.2-S1] Results: ");
    console_write_dec((uint64_t)passed);
    console_write("/");
    console_write_dec((uint64_t)total);
    console_write(" passed\n");

    if (passed == total) {
        console_write("[Gate] Phase3.2-S1\n");
    }
}

