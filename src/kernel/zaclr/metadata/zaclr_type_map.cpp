#include <kernel/zaclr/metadata/zaclr_type_map.h>

#include <kernel/support/kernel_memory.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static bool text_equals(const char* left, const char* right)
    {
        size_t index = 0u;

        if (left == NULL || right == NULL)
        {
            return left == right;
        }

        while (left[index] != '\0' || right[index] != '\0')
        {
            if (left[index] != right[index])
            {
                return false;
            }

            ++index;
        }

        return true;
    }

    static struct zaclr_token decode_typedef_or_ref_token(uint32_t coded_index)
    {
        uint32_t row;
        uint32_t tag;

        if (coded_index == 0u)
        {
            return zaclr_token_make(0u);
        }

        row = coded_index >> 2;
        tag = coded_index & 0x3u;
        switch (tag)
        {
            case 0u:
                return zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | row);
            case 1u:
                return zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEREF << 24) | row);
            case 2u:
                return zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPESPEC << 24) | row);
            default:
                return zaclr_token_make(0u);
        }
    }
}
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>

extern "C" struct zaclr_result zaclr_type_map_initialize(struct zaclr_type_map* map,
                                                          const struct zaclr_metadata_reader* reader,
                                                          struct zaclr_method_map* methods)
{
    uint32_t type_count;
    struct zaclr_type_desc* types;
    uint32_t type_index;

    if (map == NULL || reader == NULL || methods == NULL) {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    map->types = NULL;
    map->count = 0u;
    map->flags = 0u;

    type_count = zaclr_metadata_reader_get_row_count(reader, ZACLR_TOKEN_TABLE_TYPEDEF);
    if (type_count == 0u) {
        return zaclr_result_ok();
    }

    types = (struct zaclr_type_desc*)kernel_alloc(sizeof(struct zaclr_type_desc) * type_count);
    if (types == NULL) {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_METADATA);
    }

    kernel_memset(types, 0, sizeof(struct zaclr_type_desc) * type_count);

    for (type_index = 0u; type_index < type_count; ++type_index) {
        struct zaclr_typedef_row row;
        struct zaclr_name_view type_name;
        struct zaclr_name_view type_namespace;
        struct zaclr_result result;
        uint32_t next_field_list;
        uint32_t next_method_list;

        result = zaclr_metadata_reader_get_typedef_row(reader, type_index + 1u, &row);
        if (result.status != ZACLR_STATUS_OK) {
            kernel_free(types);
            return result;
        }

        result = zaclr_metadata_reader_get_string(reader, row.name_index, &type_name);
        if (result.status != ZACLR_STATUS_OK) {
            kernel_free(types);
            return result;
        }

        result = zaclr_metadata_reader_get_string(reader, row.namespace_index, &type_namespace);
        if (result.status != ZACLR_STATUS_OK) {
            kernel_free(types);
            return result;
        }

        if ((type_index + 1u) < type_count) {
            struct zaclr_typedef_row next_row;
            result = zaclr_metadata_reader_get_typedef_row(reader, type_index + 2u, &next_row);
            if (result.status != ZACLR_STATUS_OK) {
                kernel_free(types);
                return result;
            }
            next_field_list = next_row.field_list;
            next_method_list = next_row.method_list;
        } else {
            next_field_list = zaclr_metadata_reader_get_row_count(reader, ZACLR_TOKEN_TABLE_FIELD) + 1u;
            next_method_list = methods->count + 1u;
        }

        types[type_index].id = type_index + 1u;
        types[type_index].token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | (type_index + 1u));
        types[type_index].type_namespace = type_namespace;
        types[type_index].type_name = type_name;
        types[type_index].extends = decode_typedef_or_ref_token(row.extends);
        types[type_index].field_list = row.field_list;
        types[type_index].field_count = next_field_list > row.field_list ? (next_field_list - row.field_list) : 0u;
        types[type_index].method_list = row.method_list;
        types[type_index].first_method_index = row.method_list > 0u ? (row.method_list - 1u) : 0u;
        types[type_index].method_count = next_method_list > row.method_list ? (next_method_list - row.method_list) : 0u;
        types[type_index].flags = row.flags;
        if (type_namespace.text != NULL
            && type_name.text != NULL
            && text_equals(type_namespace.text, "System")
            && text_equals(type_name.text, "ModuleHandle"))
        {
            console_write("[ZACLR][typemap] System.ModuleHandle field_list=");
            console_write_dec((uint64_t)row.field_list);
            console_write(" next_field_list=");
            console_write_dec((uint64_t)next_field_list);
            console_write(" field_count=");
            console_write_dec((uint64_t)types[type_index].field_count);
            console_write(" method_list=");
            console_write_dec((uint64_t)row.method_list);
            console_write(" next_method_list=");
            console_write_dec((uint64_t)next_method_list);
            console_write(" method_count=");
            console_write_dec((uint64_t)types[type_index].method_count);
            console_write("\n");
        }
        if (type_namespace.text != NULL
            && type_name.text != NULL
            && text_equals(type_namespace.text, "System")
            && text_equals(type_name.text, "String")) {
            types[type_index].runtime_flags |= ZACLR_TYPE_RUNTIME_FLAG_HAS_COMPONENT_SIZE;
        }
    }

    for (type_index = 0u; type_index < type_count; ++type_index) {
        uint32_t method_index;
        uint32_t first_method = types[type_index].first_method_index;
        uint32_t method_count_for_type = types[type_index].method_count;

        for (method_index = 0u; method_index < method_count_for_type; ++method_index) {
            if ((first_method + method_index) < methods->count) {
                methods->methods[first_method + method_index].owning_type_token = types[type_index].token;
                if (methods->methods[first_method + method_index].name.text != NULL
                    && text_equals(methods->methods[first_method + method_index].name.text, "Finalize")
                    && methods->methods[first_method + method_index].signature.parameter_count == 0u) {
                    types[type_index].runtime_flags |= ZACLR_TYPE_RUNTIME_FLAG_HAS_FINALIZER;
                }
            }
        }
    }

    map->types = types;
    map->count = type_count;
    return zaclr_result_ok();
}

extern "C" const struct zaclr_type_desc* zaclr_type_map_find_by_token(const struct zaclr_type_map* map,
                                                                        struct zaclr_token token)
{
    uint32_t row;

    if (map == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF)) {
        return NULL;
    }

    row = zaclr_token_row(&token);
    if (row == 0u || row > map->count) {
        return NULL;
    }

    return &map->types[row - 1u];
}

extern "C" void zaclr_type_map_reset(struct zaclr_type_map* map)
{
    if (map == NULL) {
        return;
    }

    if (map->types != NULL) {
        kernel_free(map->types);
    }

    map->types = NULL;
    map->count = 0u;
    map->flags = 0u;
}

extern "C" uint32_t zaclr_type_flags(const struct zaclr_type_desc* type)
{
    return type != NULL ? type->flags : 0u;
}

extern "C" uint32_t zaclr_type_runtime_flags(const struct zaclr_type_desc* type)
{
    return type != NULL ? type->runtime_flags : 0u;
}
