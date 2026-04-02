/*
 * Zapada - src/kernel/gates/gate_pe_helpers.h
 *
 * PE helper layer self-tests.
 *
 * Tests the Zapada-owned PE parsing helpers against real .NET DLL images
 * from the initramfs ramdisk. Validates ECMA/CLI PE parsing:
 *
 *   T01  Null image rejected
 *   T02  Too-small image rejected
 *   T03  Non-PE data rejected (bad MZ)
 *   T04  Real DLL has valid MZ+PE signature
 *   T05  Real DLL has COR header (HasCorHeader)
 *   T06  Full state init succeeds on real DLL
 *   T07  BSJB metadata magic verified
 *   T08  Metadata base pointer is non-null
 *   T09  Stream table (#~ or #-) found
 *   T10  #Strings stream found
 *   T11  Entry point token matches existing pe_load result
 *   T12  RVA-to-offset round-trip for metadata RVA
 *   T13  Second DLL also parses correctly
 *   T14  Non-CLI image (System.Runtime.dll facade) has no entry point
 *   T15  Boot DLL (Zapada.Boot.dll) has non-zero entry point token
 *
 * Gate emitted when all tests pass: [Gate] PE-Helpers
 */

#ifndef ZAPADA_GATE_PE_HELPERS_H
#define ZAPADA_GATE_PE_HELPERS_H

/*
 * gate_pe_helpers_run - execute all PE helper self-tests.
 *
 * Writes per-test [PASS]/[FAIL] lines to the console.
 * Emits "[Gate] PE-Helpers" when all tests pass.
 */
void gate_pe_helpers_run(void);

#endif /* ZAPADA_GATE_PE_HELPERS_H */
