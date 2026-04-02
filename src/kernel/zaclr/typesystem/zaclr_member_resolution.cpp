#include <kernel/zaclr/typesystem/zaclr_member_resolution.h>

#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/interop/zaclr_native_assembly.h>
#include <kernel/zaclr/loader/zaclr_binder.h>

namespace
{
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

    reader = &assembly->metadata;
    table = &reader->tables[ZACLR_TOKEN_TABLE_MEMBERREF];
    row_id = zaclr_token_row(&token);
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

    result = zaclr_metadata_reader_get_string(reader, name_index, &view);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    out_info->key.method_name = view.text;
    out_info->key.type_namespace = "";
    out_info->key.type_name = "";
    out_info->assembly_name = assembly->assembly_name.text;

    if ((class_token & 0x7u) == 1u)
    {
        const char* method_name = out_info->key.method_name;

        result = zaclr_metadata_get_typeref_name(reader, class_token >> 3, &out_info->key);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        out_info->key.method_name = method_name;

        result = zaclr_metadata_get_typeref_assembly_name(reader, class_token >> 3, &out_info->assembly_name);
        if (result.status != ZACLR_STATUS_OK)
        {
            out_info->assembly_name = NULL;
        }
    }

    result = zaclr_metadata_reader_get_blob(reader, signature_index, &signature_blob);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

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

    ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "MemberRef.ResolveAssembly",
                      0u);
    ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      memberref->assembly_name,
                      0u);
    ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      memberref->key.type_name,
                      0u);
    ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      memberref->key.method_name,
                      0u);
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

        *out_method = candidate;
        break;
    }

    if (*out_method == NULL)
    {
        ZACLR_TRACE_VALUE((struct zaclr_runtime*)runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "MemberRef.MethodResolveFailed",
                          0u);
    }

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
    if (zaclr_binder_load_assembly_by_name((struct zaclr_runtime*)runtime, memberref->assembly_name, out_assembly).status != ZACLR_STATUS_OK)
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
