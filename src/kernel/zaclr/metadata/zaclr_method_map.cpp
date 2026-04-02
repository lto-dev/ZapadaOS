#include <kernel/zaclr/metadata/zaclr_method_map.h>

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>

extern "C" struct zaclr_result zaclr_method_map_initialize(struct zaclr_method_map* map,
                                                            const struct zaclr_metadata_reader* reader)
{
    uint32_t method_count;
    struct zaclr_method_desc* methods;
    uint32_t method_index;

    if (map == NULL || reader == NULL) {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    map->methods = NULL;
    map->count = 0u;
    map->flags = 0u;

    method_count = zaclr_metadata_reader_get_row_count(reader, ZACLR_TOKEN_TABLE_METHOD);
    if (method_count == 0u) {
        return zaclr_result_ok();
    }

    methods = (struct zaclr_method_desc*)kernel_alloc(sizeof(struct zaclr_method_desc) * method_count);
    if (methods == NULL) {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_METADATA);
    }

    kernel_memset(methods, 0, sizeof(struct zaclr_method_desc) * method_count);

    for (method_index = 0u; method_index < method_count; ++method_index) {
        struct zaclr_methoddef_row row;
        struct zaclr_slice signature_blob;
        struct zaclr_name_view method_name;
        struct zaclr_result result;

        result = zaclr_metadata_reader_get_methoddef_row(reader, method_index + 1u, &row);
        if (result.status != ZACLR_STATUS_OK) {
            kernel_free(methods);
            return result;
        }

        result = zaclr_metadata_reader_get_blob(reader, row.signature_blob_index, &signature_blob);
        if (result.status != ZACLR_STATUS_OK) {
            kernel_free(methods);
            return result;
        }

        result = zaclr_metadata_reader_get_string(reader, row.name_index, &method_name);
        if (result.status != ZACLR_STATUS_OK) {
            kernel_free(methods);
            return result;
        }

        methods[method_index].id = method_index + 1u;
        methods[method_index].token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_METHOD << 24) | (method_index + 1u));
        methods[method_index].name = method_name;
        methods[method_index].rva = row.rva;
        methods[method_index].impl_flags = row.impl_flags;
        methods[method_index].method_flags = row.flags;
        methods[method_index].param_list = row.param_list;
        result = zaclr_signature_parse_method(&signature_blob, &methods[method_index].signature);
        if (result.status != ZACLR_STATUS_OK) {
            kernel_free(methods);
            return result;
        }
    }

    for (uint32_t implmap_index = 1u; implmap_index <= zaclr_metadata_reader_get_row_count(reader, 0x1Cu); ++implmap_index) {
        struct zaclr_implmap_row implmap_row = {};
        struct zaclr_moduleref_row moduleref_row = {};
        struct zaclr_name_view import_name = {};
        struct zaclr_name_view module_name = {};
        struct zaclr_result result;
        uint32_t forwarded_tag;
        uint32_t forwarded_row;

        result = zaclr_metadata_reader_get_implmap_row(reader, implmap_index, &implmap_row);
        if (result.status != ZACLR_STATUS_OK) {
            kernel_free(methods);
            return result;
        }

        forwarded_tag = implmap_row.member_forwarded_coded_index & 0x1u;
        forwarded_row = implmap_row.member_forwarded_coded_index >> 1;
        if (forwarded_tag != 1u || forwarded_row == 0u || forwarded_row > method_count) {
            continue;
        }

        result = zaclr_metadata_reader_get_string(reader, implmap_row.import_name_index, &import_name);
        if (result.status != ZACLR_STATUS_OK) {
            kernel_free(methods);
            return result;
        }

        result = zaclr_metadata_reader_get_moduleref_row(reader, implmap_row.import_scope, &moduleref_row);
        if (result.status != ZACLR_STATUS_OK) {
            kernel_free(methods);
            return result;
        }

        result = zaclr_metadata_reader_get_string(reader, moduleref_row.name_index, &module_name);
        if (result.status != ZACLR_STATUS_OK) {
            kernel_free(methods);
            return result;
        }

        methods[forwarded_row - 1u].pinvoke_import_name = import_name;
        methods[forwarded_row - 1u].pinvoke_module_name = module_name;
    }

    map->methods = methods;
    map->count = method_count;
    return zaclr_result_ok();
}

extern "C" const struct zaclr_method_desc* zaclr_method_map_find_by_token(const struct zaclr_method_map* map,
                                                                            struct zaclr_token token)
{
    uint32_t row;

    if (map == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_METHOD)) {
        return NULL;
    }

    row = zaclr_token_row(&token);
    if (row == 0u || row > map->count) {
        return NULL;
    }

    return &map->methods[row - 1u];
}

extern "C" void zaclr_method_map_reset(struct zaclr_method_map* map)
{
    if (map == NULL) {
        return;
    }

    if (map->methods != NULL) {
        kernel_free(map->methods);
    }

    map->methods = NULL;
    map->count = 0u;
    map->flags = 0u;
}

extern "C" uint32_t zaclr_method_flags(const struct zaclr_method_desc* method)
{
    return method != NULL ? method->method_flags : 0u;
}
