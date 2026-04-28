#include <kernel/zaclr/typesystem/zaclr_type_system.h>

#include <kernel/zaclr/loader/zaclr_binder.h>
#include <kernel/zaclr/typesystem/zaclr_type_identity.h>

#include <kernel/support/kernel_memory.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static struct zaclr_result bind_domain_assembly(struct zaclr_runtime* runtime,
                                                    const char* assembly_name,
                                                    const struct zaclr_loaded_assembly** out_assembly)
    {
        struct zaclr_assembly_identity identity = {};
        struct zaclr_app_domain* domain;
        uint32_t length = 0u;

        if (runtime == NULL || assembly_name == NULL || out_assembly == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
        }

        domain = zaclr_runtime_current_domain(runtime);
        if (domain == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_METADATA);
        }

        while (assembly_name[length] != '\0')
        {
            ++length;
        }

        identity.name = assembly_name;
        identity.name_length = length;
        return zaclr_binder_bind(&runtime->loader, domain, &identity, out_assembly);
    }

    static uint16_t read_u16(const uint8_t* data)
    {
        return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    }

    static uint32_t read_u32(const uint8_t* data)
    {
        return (uint32_t)data[0]
             | ((uint32_t)data[1] << 8)
             | ((uint32_t)data[2] << 16)
             | ((uint32_t)data[3] << 24);
    }

    static struct zaclr_result metadata_get_typedef_name(const struct zaclr_loaded_assembly* assembly,
                                                         uint32_t row_1based,
                                                         struct zaclr_member_name_ref* out_name)
    {
        const struct zaclr_type_desc* type;

        if (assembly == NULL || out_name == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
        }

        type = zaclr_type_map_find_by_token(&assembly->type_map,
                                            zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | row_1based));
        if (type == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
        }

        out_name->type_namespace = type->type_namespace.text;
        out_name->type_name = type->type_name.text;
        out_name->method_name = "";
        return zaclr_result_ok();
    }

    static struct zaclr_result decode_compressed_uint(const struct zaclr_slice* blob,
                                                      uint32_t* offset,
                                                      uint32_t* value)
    {
        uint8_t first;

        if (blob == NULL || blob->data == NULL || offset == NULL || value == NULL || *offset >= blob->size)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_METADATA);
        }

        first = blob->data[*offset];
        if ((first & 0x80u) == 0u)
        {
            *value = first;
            *offset += 1u;
            return zaclr_result_ok();
        }

        if ((first & 0xC0u) == 0x80u)
        {
            if ((*offset + 1u) >= blob->size)
            {
                return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_METADATA);
            }

            *value = (((uint32_t)(first & 0x3Fu)) << 8) | (uint32_t)blob->data[*offset + 1u];
            *offset += 2u;
            return zaclr_result_ok();
        }

        if ((first & 0xE0u) != 0xC0u || (*offset + 3u) >= blob->size)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_METADATA);
        }

        *value = (((uint32_t)(first & 0x1Fu)) << 24)
               | ((uint32_t)blob->data[*offset + 1u] << 16)
               | ((uint32_t)blob->data[*offset + 2u] << 8)
               | (uint32_t)blob->data[*offset + 3u];
        *offset += 4u;
        return zaclr_result_ok();
    }
}

extern "C" void zaclr_type_system_reset_typespec_desc(struct zaclr_typespec_desc* desc)
{
    if (desc == NULL)
    {
        return;
    }

    zaclr_generic_context_reset(&desc->generic_context);
    *desc = {};
}

extern "C" bool zaclr_text_equals(const char* left, const char* right)
{
    size_t index = 0u;

    if (left == NULL || right == NULL)
    {
        return false;
    }

    while (left[index] != '\0' && right[index] != '\0')
    {
        if (left[index] != right[index])
        {
            return false;
        }

        ++index;
    }

    return left[index] == right[index];
}

extern "C" struct zaclr_result zaclr_metadata_get_typeref_name(const struct zaclr_metadata_reader* reader,
                                                                 uint32_t row_1based,
                                                                 struct zaclr_member_name_ref* out_name)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* row;
    uint32_t offset;
    uint32_t string_index;
    struct zaclr_name_view view;
    struct zaclr_result result;

    if (reader == NULL || out_name == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    table = &reader->tables[ZACLR_TOKEN_TABLE_TYPEREF];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    row = table->rows + ((row_1based - 1u) * table->row_size);
    offset = ((reader->row_counts[0x00u] > 0x3FFFu || reader->row_counts[0x1Au] > 0x3FFFu || reader->row_counts[0x23u] > 0x3FFFu || reader->row_counts[0x01u] > 0x3FFFu) ? 4u : 2u);

    string_index = (reader->heap_sizes & 0x01u) != 0u ? read_u32(row + offset) : read_u16(row + offset);
    result = zaclr_metadata_reader_get_string(reader, string_index, &view);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }
    out_name->type_name = view.text;

    offset += (reader->heap_sizes & 0x01u) != 0u ? 4u : 2u;
    string_index = (reader->heap_sizes & 0x01u) != 0u ? read_u32(row + offset) : read_u16(row + offset);
    result = zaclr_metadata_reader_get_string(reader, string_index, &view);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    out_name->type_namespace = view.text;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_get_assemblyref_name(const struct zaclr_metadata_reader* reader,
                                                                     uint32_t row_1based,
                                                                     const char** out_name)
{
    struct zaclr_assemblyref_row row = {};
    struct zaclr_name_view view;
    struct zaclr_result result;

    if (reader == NULL || out_name == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    result = zaclr_metadata_reader_get_assemblyref_row(reader, row_1based, &row);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_metadata_reader_get_string(reader, row.name_index, &view);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    *out_name = view.text;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_get_typeref_assembly_name(const struct zaclr_metadata_reader* reader,
                                                                          uint32_t row_1based,
                                                                          const char** out_assembly_name)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* row;
    uint32_t resolution_scope;
    uint32_t row_id;

    if (reader == NULL || out_assembly_name == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    table = &reader->tables[ZACLR_TOKEN_TABLE_TYPEREF];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    row = table->rows + ((row_1based - 1u) * table->row_size);
    resolution_scope = ((reader->row_counts[0x00u] > 0x3FFFu || reader->row_counts[0x1Au] > 0x3FFFu || reader->row_counts[0x23u] > 0x3FFFu || reader->row_counts[0x01u] > 0x3FFFu)
        ? read_u32(row)
        : read_u16(row));

    if ((resolution_scope & 0x3u) != 0x2u)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    row_id = resolution_scope >> 2;
    return zaclr_metadata_get_assemblyref_name(reader, row_id, out_assembly_name);
}

extern "C" struct zaclr_result zaclr_metadata_get_type_name(const struct zaclr_loaded_assembly* assembly,
                                                              struct zaclr_token token,
                                                              struct zaclr_member_name_ref* out_name)
{
    if (assembly == NULL || out_name == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF))
    {
        return metadata_get_typedef_name(assembly, zaclr_token_row(&token), out_name);
    }

    if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEREF))
    {
        return zaclr_metadata_get_typeref_name(&assembly->metadata, zaclr_token_row(&token), out_name);
    }

    if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPESPEC))
    {
        struct zaclr_type_identity identity = {};
        const struct zaclr_loaded_assembly* identity_assembly;
        struct zaclr_result result = zaclr_type_identity_from_token(NULL, assembly, NULL, token, &identity);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        identity_assembly = identity.assembly != NULL ? identity.assembly : assembly;
        result = zaclr_metadata_get_type_name(identity_assembly, identity.token, out_name);
        zaclr_type_identity_reset(&identity);
        return result;
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_METADATA);
}

extern "C" const struct zaclr_type_desc* zaclr_type_system_find_type_by_name(const struct zaclr_loaded_assembly* assembly,
                                                                               const struct zaclr_member_name_ref* name)
{
    uint32_t type_index;

    if (assembly == NULL || name == NULL)
    {
        return NULL;
    }

    for (type_index = 0u; type_index < assembly->type_map.count; ++type_index)
    {
        const struct zaclr_type_desc* type = &assembly->type_map.types[type_index];
        if (!zaclr_text_equals(type->type_name.text, name->type_name))
        {
            continue;
        }

        if (name->type_namespace == NULL)
        {
            if (type->type_namespace.length == 0u)
            {
                return type;
            }
        }
        else if (zaclr_text_equals(type->type_namespace.text, name->type_namespace))
        {
            return type;
        }
    }

    return NULL;
}

extern "C" struct zaclr_result zaclr_type_system_resolve_exported_type_forwarder(const struct zaclr_loaded_assembly* assembly,
                                                                                   struct zaclr_runtime* runtime,
                                                                                   const struct zaclr_member_name_ref* name,
                                                                                   const struct zaclr_loaded_assembly** out_assembly,
                                                                                   const struct zaclr_type_desc** out_type)
{
    uint32_t row_index;

    if (assembly == NULL || runtime == NULL || name == NULL || out_assembly == NULL || out_type == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    for (row_index = 1u; row_index <= zaclr_metadata_reader_get_row_count(&assembly->metadata, 0x27u); ++row_index)
    {
        struct zaclr_exportedtype_row row = {};
        struct zaclr_name_view exported_name = {};
        struct zaclr_name_view exported_namespace = {};
        struct zaclr_result result;
        uint32_t implementation_tag;
        uint32_t implementation_row;
        const char* forwarded_assembly_name = NULL;

        result = zaclr_metadata_reader_get_exportedtype_row(&assembly->metadata, row_index, &row);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_metadata_reader_get_string(&assembly->metadata, row.name_index, &exported_name);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_metadata_reader_get_string(&assembly->metadata, row.namespace_index, &exported_namespace);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (!zaclr_text_equals(exported_name.text, name->type_name)
            || !zaclr_text_equals(exported_namespace.text, name->type_namespace))
        {
            continue;
        }

        implementation_tag = row.implementation_coded_index & 0x3u;
        implementation_row = row.implementation_coded_index >> 2u;
        if (implementation_tag != 0x1u || implementation_row == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_METADATA);
        }

        result = zaclr_metadata_get_assemblyref_name(&assembly->metadata, implementation_row, &forwarded_assembly_name);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = bind_domain_assembly(runtime, forwarded_assembly_name, out_assembly);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_type = zaclr_type_system_find_type_by_name(*out_assembly, name);
        return *out_type != NULL
            ? zaclr_result_ok()
            : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
}

extern "C" struct zaclr_result zaclr_type_system_resolve_type_desc(const struct zaclr_loaded_assembly* current_assembly,
                                                                     struct zaclr_runtime* runtime,
                                                                     struct zaclr_token token,
                                                                     const struct zaclr_loaded_assembly** out_assembly,
                                                                     const struct zaclr_type_desc** out_type)
{
    if (current_assembly == NULL || runtime == NULL || out_assembly == NULL || out_type == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    *out_assembly = NULL;
    *out_type = NULL;

    if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF))
    {
        *out_assembly = current_assembly;
        *out_type = zaclr_type_map_find_by_token(&current_assembly->type_map, token);
        return *out_type != NULL
            ? zaclr_result_ok()
            : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEREF))
    {
        struct zaclr_member_name_ref name = {};
        const char* assembly_name = NULL;
        struct zaclr_result result = zaclr_metadata_get_typeref_name(&current_assembly->metadata, zaclr_token_row(&token), &name);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_metadata_get_typeref_assembly_name(&current_assembly->metadata, zaclr_token_row(&token), &assembly_name);
        if (result.status != ZACLR_STATUS_OK || assembly_name == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
        }

        return zaclr_type_system_resolve_external_named_type(runtime,
                                                             assembly_name,
                                                             &name,
                                                             out_assembly,
                                                             out_type);
    }

    if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPESPEC))
    {
        struct zaclr_type_identity identity = {};
        struct zaclr_result result = zaclr_type_identity_from_token(NULL,
                                                                    current_assembly,
                                                                    runtime,
                                                                    token,
                                                                    &identity);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_type_system_resolve_type_desc(identity.assembly != NULL ? identity.assembly : current_assembly,
                                                     runtime,
                                                     identity.token,
                                                     out_assembly,
                                                     out_type);
        zaclr_type_identity_reset(&identity);
        return result;
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_METADATA);
}

extern "C" struct zaclr_result zaclr_type_system_resolve_external_named_type(struct zaclr_runtime* runtime,
                                                                                const char* preferred_assembly_name,
                                                                                const struct zaclr_member_name_ref* name,
                                                                                const struct zaclr_loaded_assembly** out_assembly,
                                                                                const struct zaclr_type_desc** out_type)
{
    struct zaclr_result result;

    if (runtime == NULL || name == NULL || out_assembly == NULL || out_type == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    *out_assembly = NULL;
    *out_type = NULL;

    if (preferred_assembly_name != NULL)
    {
        result = bind_domain_assembly(runtime, preferred_assembly_name, out_assembly);
        if (result.status == ZACLR_STATUS_OK)
        {
            *out_type = zaclr_type_system_find_type_by_name(*out_assembly, name);
            if (*out_type != NULL)
            {
                return zaclr_result_ok();
            }

            result = zaclr_type_system_resolve_exported_type_forwarder(*out_assembly,
                                                                       runtime,
                                                                       name,
                                                                       out_assembly,
                                                                       out_type);
            if (result.status == ZACLR_STATUS_OK)
            {
                return result;
            }
        }
    }

    {
        struct zaclr_app_domain* domain = zaclr_runtime_current_domain(runtime);
        if (domain != NULL)
        {
            for (uint32_t assembly_index = 0u; assembly_index < domain->registry.count; ++assembly_index)
            {
                const struct zaclr_loaded_assembly* candidate = &domain->registry.entries[assembly_index];
                *out_type = zaclr_type_system_find_type_by_name(candidate, name);
                if (*out_type != NULL)
                {
                    *out_assembly = candidate;
                    return zaclr_result_ok();
                }

                result = zaclr_type_system_resolve_exported_type_forwarder(candidate,
                                                                           runtime,
                                                                           name,
                                                                           out_assembly,
                                                                           out_type);
                if (result.status == ZACLR_STATUS_OK)
                {
                    return result;
                }
            }
        }
    }

    {
        struct zaclr_app_domain* domain = zaclr_runtime_current_domain(runtime);
        if (domain != NULL)
        {
            for (uint32_t assembly_index = 0u; assembly_index < domain->registry.count; ++assembly_index)
            {
                const struct zaclr_loaded_assembly* candidate = &domain->registry.entries[assembly_index];
                *out_type = zaclr_type_system_find_type_by_name(candidate, name);
                if (*out_type != NULL)
                {
                    *out_assembly = candidate;
                    return zaclr_result_ok();
                }

                result = zaclr_type_system_resolve_exported_type_forwarder(candidate,
                                                                           runtime,
                                                                           name,
                                                                           out_assembly,
                                                                           out_type);
                if (result.status == ZACLR_STATUS_OK)
                {
                    return result;
                }
            }
        }
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
}

extern "C" struct zaclr_result zaclr_type_system_parse_typespec(const struct zaclr_loaded_assembly* current_assembly,
                                                                  struct zaclr_runtime* runtime,
                                                                  struct zaclr_token token,
                                                                  struct zaclr_typespec_desc* out_desc)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* row;
    uint32_t blob_index;
    struct zaclr_slice blob = {};
    uint32_t offset = 0u;
    uint32_t coded_value;
    const struct zaclr_type_desc* generic_type = NULL;
    struct zaclr_result result;

    if (current_assembly == NULL || out_desc == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPESPEC))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    *out_desc = {};
    table = &current_assembly->metadata.tables[ZACLR_TOKEN_TABLE_TYPESPEC];
    if (zaclr_token_row(&token) == 0u || zaclr_token_row(&token) > table->row_count || table->rows == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    row = table->rows + ((zaclr_token_row(&token) - 1u) * table->row_size);
    blob_index = (current_assembly->metadata.heap_sizes & 0x04u) != 0u ? read_u32(row) : read_u16(row);
    result = zaclr_metadata_reader_get_blob(&current_assembly->metadata, blob_index, &blob);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (blob.size == 0u || blob.data == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_METADATA);
    }

    out_desc->element_type = blob.data[offset++];
    if (out_desc->element_type != ZACLR_ELEMENT_TYPE_GENERICINST)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_METADATA);
    }

    if (offset >= blob.size)
    {
        return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_METADATA);
    }

    if (blob.data[offset] != ZACLR_ELEMENT_TYPE_CLASS && blob.data[offset] != ZACLR_ELEMENT_TYPE_VALUETYPE)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_METADATA);
    }

    ++offset;
    result = decode_compressed_uint(&blob, &offset, &coded_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    out_desc->generic_type_token = zaclr_token_make((((coded_value & 0x3u) == 0u ? (uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF
                                                      : ((coded_value & 0x3u) == 1u ? (uint32_t)ZACLR_TOKEN_TABLE_TYPEREF
                                                                                    : (uint32_t)ZACLR_TOKEN_TABLE_TYPESPEC))
                                                     << 24)
                                                    | (coded_value >> 2u));
    out_desc->is_generic_instantiation = 1u;

    {
        struct zaclr_slice instantiation_blob = {};
        uint8_t* temp_blob;

        temp_blob = (uint8_t*)kernel_alloc((blob.size - offset) + 1u);
        if (temp_blob == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_METADATA);
        }

        temp_blob[0] = 0x0Au;
        kernel_memcpy(temp_blob + 1u, blob.data + offset, blob.size - offset);
        instantiation_blob.data = temp_blob;
        instantiation_blob.size = (blob.size - offset) + 1u;
        result = zaclr_generic_context_set_type_instantiation(&out_desc->generic_context,
                                                              current_assembly,
                                                              runtime,
                                                              &instantiation_blob);
        kernel_free(temp_blob);
        if (result.status != ZACLR_STATUS_OK)
        {
            zaclr_type_system_reset_typespec_desc(out_desc);
            return result;
        }
    }

    if (runtime != NULL)
    {
        result = zaclr_type_system_resolve_type_desc(current_assembly,
                                                     runtime,
                                                     out_desc->generic_type_token,
                                                     &out_desc->generic_type_assembly,
                                                     &generic_type);
        if (result.status != ZACLR_STATUS_OK && result.status != ZACLR_STATUS_NOT_IMPLEMENTED)
        {
            zaclr_type_system_reset_typespec_desc(out_desc);
            return result;
        }
    }
    else
    {
        out_desc->generic_type_assembly = current_assembly;
    }

    (void)generic_type;
    return zaclr_result_ok();
}
