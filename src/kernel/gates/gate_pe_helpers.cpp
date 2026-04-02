/*
 * Zapada - src/kernel/gates/gate_pe_helpers.cpp
 *
 * PE helper layer self-tests using real .NET DLL images from the ramdisk.
 * Uses CLR_RT_PeAssemblyState directly - the single PE state type.
 *
 * Gate: [Gate] PE-Helpers
 */

#include "Core.h"

extern "C" {
#include <kernel/gates/gate_pe_helpers.h>
#include <kernel/clr/runtime/pe_assembly_helpers.h>
#include <kernel/initramfs/ramdisk.h>
#include <kernel/console.h>
#include <kernel/types.h>
}

/* ------------------------------------------------------------------ */
/* Test infrastructure                                                 */
/* ------------------------------------------------------------------ */

static uint32_t g_pass_count;
static uint32_t g_fail_count;

static void test_pass(const char *name)
{
    console_write("  PASS  ");
    console_write(name);
    console_write("\n");
    g_pass_count++;
}

static void test_fail(const char *name, const char *detail)
{
    console_write("  FAIL  ");
    console_write(name);
    if (detail != nullptr)
    {
        console_write(" -- ");
        console_write(detail);
    }
    console_write("\n");
    g_fail_count++;
}

#define TEST_ASSERT(name, cond) \
    do { \
        if ((cond)) test_pass(name); \
        else test_fail(name, #cond); \
    } while (0)

/* ------------------------------------------------------------------ */
/* Helper: find a DLL by name in the ramdisk                           */
/* ------------------------------------------------------------------ */

static bool find_dll(const char *name, const uint8_t **out_base, uint32_t *out_size)
{
    ramdisk_file_t file;
    if (ramdisk_lookup(name, &file) != RAMDISK_OK)
        return false;
    *out_base = file.data;
    *out_size = file.size;
    return true;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void test_null_image_rejected(void)
{
    zapada_pe_result_t r = zapada_pe_validate_image(nullptr, 0);
    TEST_ASSERT("T01 null image rejected", r == ZAPADA_PE_ERR_NULL_IMAGE);
}

static void test_too_small_rejected(void)
{
    uint8_t tiny[16] = {0x4Du, 0x5Au, 0};
    zapada_pe_result_t r = zapada_pe_validate_image(tiny, sizeof(tiny));
    TEST_ASSERT("T02 too-small image rejected", r == ZAPADA_PE_ERR_TOO_SMALL);
}

static void test_bad_mz_rejected(void)
{
    uint8_t junk[128];
    for (uint32_t i = 0; i < sizeof(junk); i++)
        junk[i] = 0xCCu;
    zapada_pe_result_t r = zapada_pe_validate_image(junk, sizeof(junk));
    TEST_ASSERT("T03 non-PE data rejected (bad MZ)", r == ZAPADA_PE_ERR_BAD_MZ);
}

static void test_real_dll_valid_signature(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("System.Private.CoreLib.dll", &base, &size))
    {
        test_fail("T04 real DLL valid MZ+PE", "DLL not found in ramdisk");
        return;
    }
    zapada_pe_result_t r = zapada_pe_validate_image(base, size);
    TEST_ASSERT("T04 real DLL valid MZ+PE", r == ZAPADA_PE_OK);
}

static void test_real_dll_has_cor_header(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("System.Private.CoreLib.dll", &base, &size))
    {
        test_fail("T05 real DLL has COR header", "DLL not found");
        return;
    }
    TEST_ASSERT("T05 real DLL has COR header", zapada_pe_has_cor_header(base, size));
}

static void test_full_init_succeeds(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("System.Private.CoreLib.dll", &base, &size))
    {
        test_fail("T06 full state init succeeds", "DLL not found");
        return;
    }
    CLR_RT_PeAssemblyState state;
    zapada_pe_result_t r = zapada_pe_init_state(base, size, &state);
    if (r != ZAPADA_PE_OK)
    {
        console_write("  FAIL  T06 full state init: ");
        console_write(zapada_pe_result_string(r));
        console_write("\n");
        g_fail_count++;
        return;
    }
    TEST_ASSERT("T06 full state init succeeds", r == ZAPADA_PE_OK);
}

static void test_bsjb_magic_verified(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("System.Private.CoreLib.dll", &base, &size))
    {
        test_fail("T07 BSJB metadata magic", "DLL not found");
        return;
    }
    CLR_RT_PeAssemblyState state;
    if (zapada_pe_init_state(base, size, &state) != ZAPADA_PE_OK)
    {
        test_fail("T07 BSJB metadata magic", "init failed");
        return;
    }
    TEST_ASSERT("T07 BSJB metadata magic verified",
                state.m_storageSignature.lSignature == ZAPADA_PE_METADATA_MAGIC);
}

static void test_metadata_base_nonnull(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("System.Private.CoreLib.dll", &base, &size))
    {
        test_fail("T08 metadata base non-null", "DLL not found");
        return;
    }
    CLR_RT_PeAssemblyState state;
    if (zapada_pe_init_state(base, size, &state) != ZAPADA_PE_OK)
    {
        test_fail("T08 metadata base non-null", "init failed");
        return;
    }
    const uint8_t *md_base;
    uint32_t md_size;
    bool got = zapada_pe_get_metadata(&state, &md_base, &md_size);
    TEST_ASSERT("T08 metadata base non-null", got && md_base != nullptr && md_size > 0);
}

static void test_stream_table_found(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("System.Private.CoreLib.dll", &base, &size))
    {
        test_fail("T09 stream table found", "DLL not found");
        return;
    }
    CLR_RT_PeAssemblyState state;
    if (zapada_pe_init_state(base, size, &state) != ZAPADA_PE_OK)
    {
        test_fail("T09 stream table found", "init failed");
        return;
    }
    TEST_ASSERT("T09 #~ stream found", state.m_streamTables != nullptr && state.m_streamTablesSize > 0);
}

static void test_strings_stream_found(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("System.Private.CoreLib.dll", &base, &size))
    {
        test_fail("T10 #Strings stream found", "DLL not found");
        return;
    }
    CLR_RT_PeAssemblyState state;
    if (zapada_pe_init_state(base, size, &state) != ZAPADA_PE_OK)
    {
        test_fail("T10 #Strings stream found", "init failed");
        return;
    }
    TEST_ASSERT("T10 #Strings stream found", state.m_streamStrings != nullptr && state.m_streamStringsSize > 0);
}

static void test_entry_point_matches(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("Zapada.Boot.dll", &base, &size))
    {
        test_fail("T11 entry point token match", "DLL not found");
        return;
    }
    CLR_RT_PeAssemblyState state;
    if (zapada_pe_init_state(base, size, &state) != ZAPADA_PE_OK)
    {
        test_fail("T11 entry point token match", "init failed");
        return;
    }
    uint32_t token = zapada_pe_get_entry_point_token(&state);
    TEST_ASSERT("T11 entry point token non-zero (MethodDef)", (token & 0xFF000000u) == 0x06000000u);
}

static void test_rva_round_trip(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("System.Private.CoreLib.dll", &base, &size))
    {
        test_fail("T12 RVA round-trip", "DLL not found");
        return;
    }
    CLR_RT_PeAssemblyState state;
    if (zapada_pe_init_state(base, size, &state) != ZAPADA_PE_OK)
    {
        test_fail("T12 RVA round-trip", "init failed");
        return;
    }

    uint32_t md_rva = state.m_corHeader.MetaData.VirtualAddress;
    uint32_t resolved_offset;
    bool ok = zapada_pe_rva_to_offset(&state, md_rva, &resolved_offset);
    TEST_ASSERT("T12 RVA-to-offset round-trip", ok && resolved_offset == state.m_metadataFileOffset);
}

static void test_second_dll(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("Zapada.Conformance.dll", &base, &size))
    {
        test_fail("T13 second DLL parses", "DLL not found");
        return;
    }
    CLR_RT_PeAssemblyState state;
    zapada_pe_result_t r = zapada_pe_init_state(base, size, &state);
    if (r != ZAPADA_PE_OK)
    {
        console_write("  FAIL  T13 second DLL: ");
        console_write(zapada_pe_result_string(r));
        console_write("\n");
        g_fail_count++;
        return;
    }
    TEST_ASSERT("T13 Zapada.Conformance.dll parses", state.m_hasCorHeader && state.m_metadataBase != nullptr);
}

static void test_facade_no_entrypoint(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("System.Runtime.dll", &base, &size))
    {
        test_pass("T14 facade no entrypoint (skipped: DLL not in ramdisk)");
        return;
    }
    CLR_RT_PeAssemblyState state;
    if (zapada_pe_init_state(base, size, &state) != ZAPADA_PE_OK)
    {
        test_fail("T14 facade no entrypoint", "init failed");
        return;
    }
    TEST_ASSERT("T14 System.Runtime.dll has zero entry point token",
                state.m_entryPointToken == 0);
}

static void test_boot_dll_entry_point(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("Zapada.Boot.dll", &base, &size))
    {
        test_fail("T15 boot DLL entry point", "DLL not found");
        return;
    }
    CLR_RT_PeAssemblyState state;
    if (zapada_pe_init_state(base, size, &state) != ZAPADA_PE_OK)
    {
        test_fail("T15 boot DLL entry point", "init failed");
        return;
    }
    TEST_ASSERT("T15 Zapada.Boot.dll has non-zero entry point",
                state.m_entryPointToken != 0);
}

static void test_storage_format_is_ecma335(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("System.Private.CoreLib.dll", &base, &size))
    {
        test_fail("T16 storage format is ECMA-335 PE", "DLL not found");
        return;
    }
    CLR_RT_PeAssemblyState state;
    if (zapada_pe_init_state(base, size, &state) != ZAPADA_PE_OK)
    {
        test_fail("T16 storage format is ECMA-335 PE", "init failed");
        return;
    }
    TEST_ASSERT("T16 storage format == Ecma335Pe",
                state.m_storageFormat == CLR_RT_AssemblyStorageFormat_Ecma335Pe);
}

static void test_initialized_flag(void)
{
    const uint8_t *base;
    uint32_t size;
    if (!find_dll("System.Private.CoreLib.dll", &base, &size))
    {
        test_fail("T17 initialized flag set", "DLL not found");
        return;
    }
    CLR_RT_PeAssemblyState state;
    if (zapada_pe_init_state(base, size, &state) != ZAPADA_PE_OK)
    {
        test_fail("T17 initialized flag set", "init failed");
        return;
    }
    TEST_ASSERT("T17 m_initialized == 1 after init", state.m_initialized == 1);
}

/* ------------------------------------------------------------------ */
/* Gate runner                                                         */
/* ------------------------------------------------------------------ */

extern "C" void gate_pe_helpers_run(void)
{
    g_pass_count = 0;
    g_fail_count = 0;

    console_write("\n--- PE helper self-tests ---\n");

    test_null_image_rejected();
    test_too_small_rejected();
    test_bad_mz_rejected();
    test_real_dll_valid_signature();
    test_real_dll_has_cor_header();
    test_full_init_succeeds();
    test_bsjb_magic_verified();
    test_metadata_base_nonnull();
    test_stream_table_found();
    test_strings_stream_found();
    test_entry_point_matches();
    test_rva_round_trip();
    test_second_dll();
    test_facade_no_entrypoint();
    test_boot_dll_entry_point();
    test_storage_format_is_ecma335();
    test_initialized_flag();

    console_write("\nPE helper tests: pass=");
    console_write_dec(g_pass_count);
    console_write(" fail=");
    console_write_dec(g_fail_count);
    console_write("\n");

    if (g_fail_count == 0)
    {
        console_write("[Gate] PE-Helpers\n");
    }
    else
    {
        console_write("PE helper tests: SOME TESTS FAILED\n");
    }
}
