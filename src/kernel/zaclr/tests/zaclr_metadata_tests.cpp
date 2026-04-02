#include <kernel/zaclr/loader/zaclr_assembly_registry.h>
#include <kernel/zaclr/typesystem/zaclr_member_resolution.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>
#include <kernel/support/kernel_memory.h>

namespace {

static struct zaclr_result expect(bool condition)
{
    return condition ? zaclr_result_ok()
                     : zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_DIAG);
}

static struct zaclr_result run_token_tests(void)
{
    struct zaclr_token token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_METHOD << 24) | 0x1234u);
    struct zaclr_result result = expect(zaclr_token_table(&token) == ZACLR_TOKEN_TABLE_METHOD);
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = expect(zaclr_token_row(&token) == 0x1234u);
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    return expect(!zaclr_token_is_nil(&token));
}

static struct zaclr_result run_signature_tests(void)
{
    static const uint8_t signature_blob_bytes[] = { 0x00u, 0x02u, 0x08u, 0x08u, 0x12u, 0x05u };
    struct zaclr_slice signature_blob = { signature_blob_bytes, sizeof(signature_blob_bytes) };
    struct zaclr_signature_desc signature;
    struct zaclr_signature_type parameter0;
    struct zaclr_signature_type parameter1;
    struct zaclr_result result = zaclr_signature_parse_method(&signature_blob, &signature);

    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = expect(signature.parameter_count == 2u);
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = expect(signature.return_type.element_type == ZACLR_ELEMENT_TYPE_I4);
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = zaclr_signature_read_method_parameter(&signature, 0u, &parameter0);
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = zaclr_signature_read_method_parameter(&signature, 1u, &parameter1);
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = expect(parameter0.element_type == ZACLR_ELEMENT_TYPE_I4);
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    return expect(parameter1.element_type == ZACLR_ELEMENT_TYPE_CLASS);
}

static struct zaclr_result run_registry_tests(void)
{
    struct zaclr_assembly_registry registry;
    struct zaclr_loaded_assembly assembly;
    const struct zaclr_loaded_assembly* found;
    struct zaclr_result result = zaclr_assembly_registry_initialize(&registry);

    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    assembly = {};
    assembly.id = 1u;
    assembly.assembly_name.text = "Zapada.Boot";
    assembly.assembly_name.length = 11u;

    result = zaclr_assembly_registry_register(&registry, &assembly);
    if (result.status != ZACLR_STATUS_OK) {
        zaclr_assembly_registry_reset(&registry);
        return result;
    }

    found = zaclr_assembly_registry_find_by_name(&registry, "Zapada.Boot");
    result = expect(found != NULL && found->id == 1u);
    zaclr_assembly_registry_reset(&registry);
    return result;
}

static struct zaclr_result run_type_map_field_count_tests(void)
{
    struct zaclr_type_map map = {};
    struct zaclr_type_desc* types;

    types = (struct zaclr_type_desc*)kernel_alloc(sizeof(struct zaclr_type_desc) * 2u);
    if (types == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_DIAG);
    }

    kernel_memset(types, 0, sizeof(struct zaclr_type_desc) * 2u);
    types[0].field_list = 2u;
    types[0].field_count = 3u;
    types[1].field_list = 5u;
    types[1].field_count = 1u;
    map.types = types;
    map.count = 2u;

    {
        struct zaclr_result result = expect(map.types[0].field_list + map.types[0].field_count == 5u);
        if (result.status != ZACLR_STATUS_OK)
        {
            kernel_free(types);
            return result;
        }

        result = expect(map.types[1].field_list + map.types[1].field_count == 6u);
        kernel_free(types);
        return result;
    }
}

static struct zaclr_result run_type_system_lookup_tests(void)
{
    struct zaclr_loaded_assembly assembly = {};
    struct zaclr_type_desc types[2] = {};
    struct zaclr_member_name_ref name = {};
    const struct zaclr_type_desc* found;
    struct zaclr_result result;

    types[0].token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | 1u);
    types[0].type_namespace.text = "System";
    types[0].type_namespace.length = 6u;
    types[0].type_name.text = "String";
    types[0].type_name.length = 6u;

    types[1].token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | 2u);
    types[1].type_namespace.text = "Zapada";
    types[1].type_namespace.length = 6u;
    types[1].type_name.text = "BootLoader";
    types[1].type_name.length = 10u;

    assembly.type_map.types = types;
    assembly.type_map.count = 2u;

    result = expect(zaclr_text_equals("System", "System"));
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = expect(!zaclr_text_equals("System", "String"));
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    name.type_namespace = "System";
    name.type_name = "String";
    found = zaclr_type_system_find_type_by_name(&assembly, &name);
    result = expect(found == &types[0]);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    name.type_namespace = "Zapada";
    name.type_name = "BootLoader";
    found = zaclr_type_system_find_type_by_name(&assembly, &name);
    if (found != &types[1])
    {
        return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_DIAG);
    }

    name.type_namespace = "Missing";
    name.type_name = "BootLoader";
    return expect(zaclr_type_system_find_type_by_name(&assembly, &name) == NULL);
}

static struct zaclr_result run_member_resolution_method_tests(void)
{
    static const uint8_t signature_blob_bytes[] = { 0x00u, 0x01u, 0x08u, 0x08u };
    struct zaclr_slice signature_blob = { signature_blob_bytes, sizeof(signature_blob_bytes) };
    struct zaclr_runtime runtime = {};
    struct zaclr_loaded_assembly source_assembly = {};
    struct zaclr_loaded_assembly target_assembly = {};
    struct zaclr_type_desc target_type = {};
    struct zaclr_method_desc target_method = {};
    struct zaclr_memberref_target memberref = {};
    const struct zaclr_loaded_assembly* resolved_assembly = NULL;
    const struct zaclr_type_desc* resolved_type = NULL;
    const struct zaclr_method_desc* resolved_method = NULL;
    struct zaclr_result result;

    result = zaclr_assembly_registry_initialize(&runtime.assemblies);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    target_type.token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | 1u);
    target_type.type_namespace.text = "System";
    target_type.type_namespace.length = 6u;
    target_type.type_name.text = "Console";
    target_type.type_name.length = 7u;

    target_method.token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_METHOD << 24) | 1u);
    target_method.owning_type_token = target_type.token;
    target_method.name.text = "WriteLine";
    target_method.name.length = 9u;

    result = zaclr_signature_parse_method(&signature_blob, &target_method.signature);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    target_assembly.id = 7u;
    target_assembly.assembly_name.text = "System.Console";
    target_assembly.assembly_name.length = 14u;
    target_assembly.type_map.types = &target_type;
    target_assembly.type_map.count = 1u;
    target_assembly.method_map.methods = &target_method;
    target_assembly.method_map.count = 1u;

    result = zaclr_assembly_registry_register(&runtime.assemblies, &target_assembly);
    if (result.status != ZACLR_STATUS_OK)
    {
        zaclr_assembly_registry_reset(&runtime.assemblies);
        return result;
    }

    memberref.key.type_namespace = "System";
    memberref.key.type_name = "Console";
    memberref.key.method_name = "WriteLine";
    memberref.assembly_name = "System.Console";
    result = zaclr_signature_parse_method(&signature_blob, &memberref.signature);
    if (result.status != ZACLR_STATUS_OK)
    {
        zaclr_assembly_registry_reset(&runtime.assemblies);
        return result;
    }

    result = zaclr_member_resolution_resolve_method(&runtime,
                                                    &source_assembly,
                                                    &memberref,
                                                    &resolved_assembly,
                                                    &resolved_type,
                                                    &resolved_method);
    if (result.status != ZACLR_STATUS_OK)
    {
        zaclr_assembly_registry_reset(&runtime.assemblies);
        return result;
    }

    result = expect(resolved_assembly != NULL && resolved_assembly->id == 7u);
    if (result.status == ZACLR_STATUS_OK)
    {
        result = expect(resolved_type == &runtime.assemblies.entries[0].type_map.types[0]);
    }
    if (result.status == ZACLR_STATUS_OK)
    {
        result = expect(resolved_method == &runtime.assemblies.entries[0].method_map.methods[0]);
    }

    zaclr_assembly_registry_reset(&runtime.assemblies);
    return result;
}

}

extern "C" struct zaclr_result zaclr_run_metadata_tests(void)
{
    struct zaclr_result result = run_token_tests();
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = run_signature_tests();
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = run_registry_tests();
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = run_type_map_field_count_tests();
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = run_type_system_lookup_tests();
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    return run_member_resolution_method_tests();
}

extern "C" struct zaclr_result zaclr_validate_metadata_image(const struct zaclr_slice* image,
                                                              const char* expected_name,
                                                              uint32_t minimum_type_count,
                                                              uint32_t minimum_method_count)
{
    struct zaclr_loader loader;
    struct zaclr_loaded_assembly assembly;
    struct zaclr_result result;

    if (image == NULL || expected_name == NULL) {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_DIAG);
    }

    result = zaclr_loader_initialize(&loader);
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = zaclr_loader_load_image(&loader, image, &assembly);
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = expect(assembly.assembly_name.length != 0u);
    if (result.status == ZACLR_STATUS_OK) {
        size_t index;
        for (index = 0u; index < assembly.assembly_name.length; ++index) {
            if (expected_name[index] == '\0' || expected_name[index] != assembly.assembly_name.text[index]) {
                result = zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_DIAG);
                break;
            }
        }

        if (result.status == ZACLR_STATUS_OK && expected_name[assembly.assembly_name.length] != '\0') {
            result = zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_DIAG);
        }
    }

    if (result.status == ZACLR_STATUS_OK) {
        result = expect(assembly.type_map.count >= minimum_type_count);
    }

    if (result.status == ZACLR_STATUS_OK) {
        result = expect(assembly.method_map.count >= minimum_method_count);
    }

    if (result.status == ZACLR_STATUS_OK) {
        result = expect(zaclr_token_matches_table(&assembly.entry_point_token, ZACLR_TOKEN_TABLE_METHOD));
    }

    zaclr_loader_release_loaded_assembly(&assembly);
    return result;
}
