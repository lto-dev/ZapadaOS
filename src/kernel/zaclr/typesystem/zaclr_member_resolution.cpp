#include <kernel/zaclr/typesystem/zaclr_member_resolution.h>

#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/interop/zaclr_native_assembly.h>
#include <kernel/zaclr/loader/zaclr_binder.h>

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

    static struct zaclr_result decode_compressed_uint(const uint8_t* data,
                                                      size_t size,
                                                      uint32_t* offset,
                                                      uint32_t* value)
    {
        uint8_t first;

        if (data == NULL || offset == NULL || value == NULL || *offset >= size)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_METADATA);
        }

        first = data[*offset];
        if ((first & 0x80u) == 0u)
        {
            *value = first;
            *offset += 1u;
            return zaclr_result_ok();
        }

        if ((first & 0xC0u) == 0x80u)
        {
            if ((*offset + 1u) >= size)
            {
                return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_METADATA);
            }

            *value = (((uint32_t)(first & 0x3Fu)) << 8) | (uint32_t)data[*offset + 1u];
            *offset += 2u;
            return zaclr_result_ok();
        }

        if ((first & 0xE0u) == 0xC0u)
        {
            if ((*offset + 3u) >= size)
            {
                return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_METADATA);
            }

            *value = (((uint32_t)(first & 0x1Fu)) << 24)
                   | ((uint32_t)data[*offset + 1u] << 16)
                   | ((uint32_t)data[*offset + 2u] << 8)
                   | (uint32_t)data[*offset + 3u];
            *offset += 4u;
            return zaclr_result_ok();
        }

        return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_METADATA);
    }

    static struct zaclr_result populate_typespec_owner_info(const struct zaclr_loaded_assembly* assembly,
                                                            const struct zaclr_metadata_reader* reader,
                                                            uint32_t row_1based,
                                                            struct zaclr_memberref_target* out_info)
    {
        const struct zaclr_metadata_table_view* table;
        const uint8_t* row;
        uint32_t blob_index;
        struct zaclr_slice blob = {};
        uint32_t offset = 0u;
        uint32_t token_value = 0u;
        struct zaclr_token token;

        if (assembly == NULL || reader == NULL || out_info == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
        }

        table = &reader->tables[ZACLR_TOKEN_TABLE_TYPESPEC];
        if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
        }

        row = table->rows + ((row_1based - 1u) * table->row_size);
        blob_index = (reader->heap_sizes & 0x04u) != 0u ? read_u32(row) : read_u16(row);
        {
            struct zaclr_result result = zaclr_metadata_reader_get_blob(reader, blob_index, &blob);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }
        }

        if (blob.data == NULL || blob.size < 2u || blob.data[offset++] != ZACLR_ELEMENT_TYPE_GENERICINST)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_METADATA);
        }

        if (blob.data[offset] != ZACLR_ELEMENT_TYPE_CLASS && blob.data[offset] != ZACLR_ELEMENT_TYPE_VALUETYPE)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_METADATA);
        }
        ++offset;

        {
            struct zaclr_result result = decode_compressed_uint(blob.data, blob.size, &offset, &token_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }
        }

        token = zaclr_token_make(0u);
        switch (token_value & 0x3u)
        {
            case 0u:
                token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | (token_value >> 2));
                break;
            case 1u:
                token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEREF << 24) | (token_value >> 2));
                break;
            default:
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_METADATA);
        }

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            const struct zaclr_type_desc* type = zaclr_type_map_find_by_token(&assembly->type_map, token);
            if (type == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
            }

            out_info->key.type_namespace = type->type_namespace.text;
            out_info->key.type_name = type->type_name.text;
            out_info->assembly_name = assembly->assembly_name.text;
            return zaclr_result_ok();
        }

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEREF))
        {
            struct zaclr_result result = zaclr_metadata_get_typeref_name(reader, zaclr_token_row(&token), &out_info->key);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_metadata_get_typeref_assembly_name(reader, zaclr_token_row(&token), &out_info->assembly_name);
            if (result.status != ZACLR_STATUS_OK)
            {
                out_info->assembly_name = NULL;
            }
            return zaclr_result_ok();
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_METADATA);
    }
}

extern "C" struct zaclr_result zaclr_metadata_get_memberref_info(const struct zaclr_loaded_assembly* assembly,
                                                                    struct zaclr_token token,
                                                                    struct zaclr_memberref_target* out_info)
{
    const struct zaclr_metadata_reader* reader;
    const struct zaclr_metadata_table_view* table;
    const uint8_t* row;
    uint32_t offset = 0u;
    uint32_t class_token;
    uint32_t name_index;
    uint32_t signature_index;
    uint32_t coded_value;
    struct zaclr_name_view view;
    struct zaclr_slice signature_blob;
    struct zaclr_result result;
    uint32_t row_id;

    if (assembly == NULL || out_info == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_MEMBERREF))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    /* console_write("[ZACLR][memberref] begin token="); */
    /* console_write_hex64((uint64_t)token.raw); */
    /* console_write("\n"); */

    reader = &assembly->metadata;
    table = &reader->tables[ZACLR_TOKEN_TABLE_MEMBERREF];
    row_id = zaclr_token_row(&token);
    /* console_write("[ZACLR][memberref] row_id="); */
    /* console_write_dec((uint64_t)row_id); */
    /* console_write(" row_count="); */
    /* console_write_dec((uint64_t)table->row_count); */
    /* console_write(" row_size="); */
    /* console_write_dec((uint64_t)table->row_size); */
    /* console_write("\n"); */
    if (row_id == 0u || row_id > table->row_count || table->rows == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    row = table->rows + ((row_id - 1u) * table->row_size);
    if (reader->row_counts[0x02u] >= 0x2000u || reader->row_counts[0x01u] >= 0x2000u || reader->row_counts[0x1Au] >= 0x2000u || reader->row_counts[0x06u] >= 0x2000u || reader->row_counts[0x1Bu] >= 0x2000u)
    {
        coded_value = read_u32(row + offset);
        offset += 4u;
    }
    else
    {
        coded_value = read_u16(row + offset);
        offset += 2u;
    }

    name_index = (reader->heap_sizes & 0x01u) != 0u ? read_u32(row + offset) : read_u16(row + offset);
    offset += (reader->heap_sizes & 0x01u) != 0u ? 4u : 2u;
    signature_index = (reader->heap_sizes & 0x04u) != 0u ? read_u32(row + offset) : read_u16(row + offset);
    class_token = coded_value;
    out_info->class_token = class_token;

    /* console_write("[ZACLR][memberref] coded_value="); */
    /* console_write_hex64((uint64_t)coded_value); */
    /* console_write(" class_tag="); */
    /* console_write_dec((uint64_t)(class_token & 0x7u)); */
    /* console_write(" name_index="); */
    /* console_write_dec((uint64_t)name_index); */
    /* console_write(" sig_index="); */
    /* console_write_dec((uint64_t)signature_index); */
    /* console_write("\n"); */

    result = zaclr_metadata_reader_get_string(reader, name_index, &view);
    if (result.status != ZACLR_STATUS_OK)
    {
        console_write("[ZACLR][memberref] get_string failed\n");
        return result;
    }

    /* console_write("[ZACLR][memberref] method_name len="); */
    /* console_write_dec((uint64_t)view.length); */
    /* console_write(" preview="); */
    /* for (uint32_t i = 0u; i < view.length && i < 32u; ++i)
    {
        char ch[2];
        ch[0] = view.text[i];
        ch[1] = '\0';
        console_write(ch);
    }
    console_write("\n"); */

    out_info->key.method_name = view.text;
    out_info->key.type_namespace = "";
    out_info->key.type_name = "";
    out_info->assembly_name = assembly->assembly_name.text;

    if ((class_token & 0x7u) == 0u)
    {
        /* console_write("[ZACLR][memberref] owner=typedef\n"); */
        struct zaclr_token owner_token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | (class_token >> 3));
        const struct zaclr_type_desc* owner_type = zaclr_type_map_find_by_token(&assembly->type_map, owner_token);
        if (owner_type == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
        }

        out_info->key.type_namespace = owner_type->type_namespace.text;
        out_info->key.type_name = owner_type->type_name.text;
    }
    else if ((class_token & 0x7u) == 1u)
    {
        /* console_write("[ZACLR][memberref] owner=typeref\n"); */
        const char* method_name = out_info->key.method_name;

        result = zaclr_metadata_get_typeref_name(reader, class_token >> 3, &out_info->key);
        if (result.status != ZACLR_STATUS_OK)
        {
            console_write("[ZACLR][memberref] typeref name failed\n");
            return result;
        }

        out_info->key.method_name = method_name;

        result = zaclr_metadata_get_typeref_assembly_name(reader, class_token >> 3, &out_info->assembly_name);
        if (result.status != ZACLR_STATUS_OK)
        {
            out_info->assembly_name = NULL;
        }

        /* console_write("[ZACLR][memberref] typeref resolved assembly="); */
        /* console_write(out_info->assembly_name != NULL ? out_info->assembly_name : "<null>"); */
        /* console_write(" type="); */
        /* console_write(out_info->key.type_name != NULL ? out_info->key.type_name : "<null>"); */
        /* console_write("\n"); */
    }
    else if ((class_token & 0x7u) == 4u)
    {
        /* console_write("[ZACLR][memberref] owner=typespec\n"); */
        const char* method_name = out_info->key.method_name;

        result = populate_typespec_owner_info(assembly, reader, class_token >> 3, out_info);
        if (result.status != ZACLR_STATUS_OK)
        {
            console_write("[ZACLR][memberref] typespec owner failed\n");
            return result;
        }

        out_info->key.method_name = method_name;
    }
    else if ((class_token & 0x7u) == 3u)
    {
        /* console_write("[ZACLR][memberref] owner=methoddef\n"); */
        struct zaclr_token owner_method_token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_METHOD << 24) | (class_token >> 3));
        const struct zaclr_method_desc* owner_method = zaclr_method_map_find_by_token(&assembly->method_map, owner_method_token);
        const struct zaclr_type_desc* owner_type;
        if (owner_method == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
        }

        owner_type = zaclr_type_map_find_by_token(&assembly->type_map, owner_method->owning_type_token);
        if (owner_type == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
        }

        out_info->key.type_namespace = owner_type->type_namespace.text;
        out_info->key.type_name = owner_type->type_name.text;
    }

    result = zaclr_metadata_reader_get_blob(reader, signature_index, &signature_blob);
    if (result.status != ZACLR_STATUS_OK)
    {
        console_write("[ZACLR][memberref] get_blob failed\n");
        return result;
    }

    /* console_write("[ZACLR][memberref] signature size="); */
    /* console_write_dec((uint64_t)signature_blob.size); */
    /* console_write(" first="); */
    /* console_write_hex64((uint64_t)(signature_blob.data != NULL && signature_blob.size != 0u ? signature_blob.data[0] : 0u)); */
    /* console_write("\n"); */

    if (signature_blob.data != NULL && signature_blob.size != 0u && signature_blob.data[0] == 0x06u)
    {
        out_info->signature = {};
        return zaclr_result_ok();
    }

    return zaclr_signature_parse_method(&signature_blob, &out_info->signature);
}

extern "C" struct zaclr_result zaclr_member_resolution_resolve_method(const struct zaclr_runtime* runtime,
                                                                        const struct zaclr_loaded_assembly* source_assembly,
                                                                        const struct zaclr_memberref_target* memberref,
                                                                        const struct zaclr_loaded_assembly** out_assembly,
                                                                        const struct zaclr_type_desc** out_type,
                                                                        const struct zaclr_method_desc** out_method)
{
    struct zaclr_result result;

    if (runtime == NULL || source_assembly == NULL || memberref == NULL || out_assembly == NULL || out_type == NULL || out_method == NULL || memberref->assembly_name == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    /* console_write("[ZACLR][resolve] begin assembly="); */
    /* console_write(memberref->assembly_name != NULL ? memberref->assembly_name : "<null>"); */
    /* console_write(" type="); */
    /* console_write(memberref->key.type_name != NULL ? memberref->key.type_name : "<null>"); */
    /* console_write(" method="); */
    /* console_write(memberref->key.method_name != NULL ? memberref->key.method_name : "<null>"); */
    /* console_write("\n"); */

    /* console_write("[ZACLR][resolve] trace marker\n"); */
    ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "MemberRef.ResolveAssembly",
                      0u);
    /* console_write("[ZACLR][resolve] trace assembly name\n"); */
    ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      memberref->assembly_name,
                      0u);
    /* console_write("[ZACLR][resolve] trace type name\n"); */
    ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      memberref->key.type_name,
                      0u);
    /* console_write("[ZACLR][resolve] trace method name\n"); */
    ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      memberref->key.method_name,
                      0u);
    /* console_write("[ZACLR][resolve] trace parameter count\n"); */
    ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "MemberRef.ParameterCount",
                      (uint64_t)memberref->signature.parameter_count);

    result = zaclr_type_system_resolve_external_named_type((struct zaclr_runtime*)runtime,
                                                           memberref->assembly_name,
                                                           &memberref->key,
                                                           out_assembly,
                                                           out_type);
    if (result.status != ZACLR_STATUS_OK)
    {
        ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "MemberRef.TypeResolveFailed",
                          (uint64_t)result.status);
        return result;
    }

    *out_method = NULL;
    for (uint32_t method_index = 0u; method_index < (*out_assembly)->method_map.count; ++method_index)
    {
        const struct zaclr_method_desc* candidate = &(*out_assembly)->method_map.methods[method_index];
        if (candidate->owning_type_token.raw != (*out_type)->token.raw)
        {
            continue;
        }

        if (!zaclr_text_equals(candidate->name.text, memberref->key.method_name))
        {
            continue;
        }

        if (!zaclr_managed_signatures_equal(*out_assembly,
                                            &candidate->signature,
                                            source_assembly,
                                            &memberref->signature))
        {
            ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                              ZACLR_TRACE_CATEGORY_INTEROP,
                              ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                              "MemberRef.SignatureSkip",
                              (uint64_t)candidate->token.raw);
            continue;
        }

        /* console_write("[ZACLR][resolve] method match index="); */
        /* console_write_dec((uint64_t)method_index); */
        /* console_write(" token="); */
        /* console_write_hex64((uint64_t)candidate->token.raw); */
        /* console_write("\n"); */
        *out_method = candidate;
        break;
    }

    if (*out_method == NULL)
    {
        /* console_write("[ZACLR][resolve] no matching method\n"); */
        ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "MemberRef.MethodResolveFailed",
                          0u);
    }

    /* console_write("[ZACLR][resolve] end\n"); */

    return *out_method != NULL
        ? zaclr_result_ok()
        : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
}

extern "C" struct zaclr_result zaclr_member_resolution_resolve_field(const struct zaclr_runtime* runtime,
                                                                       const struct zaclr_memberref_target* memberref,
                                                                       const struct zaclr_loaded_assembly** out_assembly,
                                                                       uint32_t* out_field_row)
{
    const struct zaclr_type_desc* target_type;
    uint32_t field_start;
    uint32_t field_end;
    uint32_t field_row;

    if (runtime == NULL || memberref == NULL || out_assembly == NULL || out_field_row == NULL || memberref->assembly_name == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    *out_field_row = 0u;
    if (bind_domain_assembly((struct zaclr_runtime*)runtime, memberref->assembly_name, out_assembly).status != ZACLR_STATUS_OK)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    target_type = zaclr_type_system_find_type_by_name(*out_assembly, &memberref->key);
    if (target_type == NULL)
    {
        struct zaclr_result result = zaclr_type_system_resolve_exported_type_forwarder(*out_assembly,
                                                                                        (struct zaclr_runtime*)runtime,
                                                                                        &memberref->key,
                                                                                        out_assembly,
                                                                                        &target_type);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
    }

    field_start = target_type->field_list;
    if (field_start == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    field_end = field_start + target_type->field_count;
    if (field_end <= field_start)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    for (field_row = field_start; field_row < field_end; ++field_row)
    {
        struct zaclr_field_row row = {};
        struct zaclr_name_view field_name = {};
        struct zaclr_result result = zaclr_metadata_reader_get_field_row(&(*out_assembly)->metadata, field_row, &row);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_metadata_reader_get_string(&(*out_assembly)->metadata, row.name_index, &field_name);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (zaclr_text_equals(field_name.text, memberref->key.method_name))
        {
            *out_field_row = field_row;
            return zaclr_result_ok();
        }
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
}
