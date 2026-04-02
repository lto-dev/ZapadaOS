#include <kernel/zaclr/interop/zaclr_native_assembly.h>

#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>
#include <kernel/zaclr/metadata/zaclr_type_map.h>

namespace
{
    struct zaclr_resolved_type_identity {
        uint8_t element_type;
        const char* type_namespace;
        const char* type_name;
    };

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
            return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_INTEROP);
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
                return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_INTEROP);
            }

            *value = (((uint32_t)(first & 0x3Fu)) << 8) | (uint32_t)data[*offset + 1u];
            *offset += 2u;
            return zaclr_result_ok();
        }

        if ((first & 0xE0u) == 0xC0u)
        {
            if ((*offset + 3u) >= size)
            {
                return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_INTEROP);
            }

            *value = (((uint32_t)(first & 0x1Fu)) << 24)
                   | ((uint32_t)data[*offset + 1u] << 16)
                   | ((uint32_t)data[*offset + 2u] << 8)
                   | (uint32_t)data[*offset + 3u];
            *offset += 4u;
            return zaclr_result_ok();
        }

        return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    static struct zaclr_result metadata_get_typeref_name(const struct zaclr_metadata_reader* reader,
                                                         uint32_t row_1based,
                                                         const char** out_namespace,
                                                         const char** out_name)
    {
        const struct zaclr_metadata_table_view* table;
        const uint8_t* row;
        uint32_t offset;
        uint32_t string_index;
        struct zaclr_name_view view;
        struct zaclr_result result;

        if (reader == NULL || out_namespace == NULL || out_name == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        table = &reader->tables[ZACLR_TOKEN_TABLE_TYPEREF];
        if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        row = table->rows + ((row_1based - 1u) * table->row_size);
        offset = ((reader->row_counts[0x00u] > 0x3FFFu
                || reader->row_counts[0x1Au] > 0x3FFFu
                || reader->row_counts[0x23u] > 0x3FFFu
                || reader->row_counts[0x01u] > 0x3FFFu) ? 4u : 2u);

        string_index = (reader->heap_sizes & 0x01u) != 0u ? read_u32(row + offset) : read_u16(row + offset);
        result = zaclr_metadata_reader_get_string(reader, string_index, &view);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_name = view.text;
        offset += (reader->heap_sizes & 0x01u) != 0u ? 4u : 2u;
        string_index = (reader->heap_sizes & 0x01u) != 0u ? read_u32(row + offset) : read_u16(row + offset);
        result = zaclr_metadata_reader_get_string(reader, string_index, &view);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_namespace = view.text;
        return zaclr_result_ok();
    }

    static struct zaclr_result resolve_type_identity(const struct zaclr_loaded_assembly* assembly,
                                                     struct zaclr_token token,
                                                     uint8_t element_type,
                                                     struct zaclr_resolved_type_identity* out_identity)
    {
        if (assembly == NULL || out_identity == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_identity = {};
        out_identity->element_type = element_type;

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            const struct zaclr_type_desc* type = zaclr_type_map_find_by_token(&assembly->type_map, token);
            if (type == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
            }

            out_identity->type_namespace = type->type_namespace.text;
            out_identity->type_name = type->type_name.text;
            return zaclr_result_ok();
        }

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEREF))
        {
            return metadata_get_typeref_name(&assembly->metadata,
                                             zaclr_token_row(&token),
                                             &out_identity->type_namespace,
                                             &out_identity->type_name);
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    static bool type_identity_equals(const struct zaclr_resolved_type_identity* left,
                                     const struct zaclr_resolved_type_identity* right)
    {
        return left != NULL
            && right != NULL
            && left->element_type == right->element_type
            && zaclr_internal_call_text_equals(left->type_namespace, right->type_namespace)
            && zaclr_internal_call_text_equals(left->type_name, right->type_name);
    }

    static bool parse_type_header(const uint8_t* data,
                                  size_t size,
                                  uint32_t* offset,
                                  uint8_t* out_element_type,
                                  uint8_t* out_flags)
    {
        if (data == NULL || offset == NULL || out_element_type == NULL || out_flags == NULL || *offset >= size)
        {
            return false;
        }

        *out_flags = 0u;
        *out_element_type = data[(*offset)++];
        while (*out_element_type == ZACLR_ELEMENT_TYPE_BYREF || *out_element_type == ZACLR_ELEMENT_TYPE_PINNED)
        {
            if (*out_element_type == ZACLR_ELEMENT_TYPE_BYREF)
            {
                *out_flags |= ZACLR_NATIVE_BIND_SIG_FLAG_BYREF;
            }
            else
            {
                *out_flags |= ZACLR_NATIVE_BIND_SIG_FLAG_PINNED;
            }

            if (*offset >= size)
            {
                return false;
            }

            *out_element_type = data[(*offset)++];
        }

        return true;
    }

    static struct zaclr_token decode_type_token(uint32_t encoded)
    {
        uint32_t tag = encoded & 0x3u;
        uint32_t row = encoded >> 2;
        uint32_t table = tag == 0u
            ? ZACLR_TOKEN_TABLE_TYPEDEF
            : (tag == 1u ? ZACLR_TOKEN_TABLE_TYPEREF : ZACLR_TOKEN_TABLE_TYPESPEC);
        return zaclr_token_make((table << 24) | row);
    }

    static bool try_get_enum_underlying_element_type(const struct zaclr_loaded_assembly* assembly,
                                                     struct zaclr_token token,
                                                     uint8_t* out_element_type)
    {
        const struct zaclr_type_desc* type;
        struct zaclr_field_row field_row = {};
        struct zaclr_name_view field_name = {};
        struct zaclr_slice field_signature = {};
        struct zaclr_result result;

        if (assembly == NULL || out_element_type == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            return false;
        }

        type = zaclr_type_map_find_by_token(&assembly->type_map, token);
        if (type == NULL || type->field_count == 0u)
        {
            return false;
        }

        result = zaclr_metadata_reader_get_field_row(&assembly->metadata, type->field_list, &field_row);
        if (result.status != ZACLR_STATUS_OK)
        {
            return false;
        }

        result = zaclr_metadata_reader_get_string(&assembly->metadata, field_row.name_index, &field_name);
        if (result.status != ZACLR_STATUS_OK || !zaclr_internal_call_text_equals(field_name.text, "value__"))
        {
            return false;
        }

        result = zaclr_metadata_reader_get_blob(&assembly->metadata, field_row.signature_blob_index, &field_signature);
        if (result.status != ZACLR_STATUS_OK || field_signature.data == NULL || field_signature.size < 2u)
        {
            return false;
        }

        if (field_signature.data[0] != 0x06u)
        {
            return false;
        }

        *out_element_type = field_signature.data[1];
        return true;
    }

    static bool managed_type_matches_bind(const struct zaclr_loaded_assembly* assembly,
                                          const uint8_t* data,
                                          size_t size,
                                          uint32_t* offset,
                                          const struct zaclr_native_bind_sig_type* bind_type)
    {
        uint8_t actual_element_type;
        uint8_t actual_flags;
        uint32_t encoded;
        struct zaclr_result result;

        if (assembly == NULL || data == NULL || offset == NULL || bind_type == NULL)
        {
            return false;
        }

        if (!parse_type_header(data, size, offset, &actual_element_type, &actual_flags)
            || actual_flags != bind_type->flags)
        {
            return false;
        }

        if (actual_element_type != bind_type->element_type)
        {
            uint8_t enum_underlying_type;
            uint32_t saved_offset = *offset;
            uint32_t encoded;
            if ((actual_element_type != ZACLR_ELEMENT_TYPE_VALUETYPE && actual_element_type != ZACLR_ELEMENT_TYPE_CLASS)
                || decode_compressed_uint(data, size, &saved_offset, &encoded).status != ZACLR_STATUS_OK
                || !try_get_enum_underlying_element_type(assembly, decode_type_token(encoded), &enum_underlying_type)
                || enum_underlying_type != bind_type->element_type)
            {
                return false;
            }

            *offset = saved_offset;
            return true;
        }

        switch (actual_element_type)
        {
            case ZACLR_ELEMENT_TYPE_CLASS:
            case ZACLR_ELEMENT_TYPE_VALUETYPE:
            {
                struct zaclr_resolved_type_identity actual = {};
                struct zaclr_resolved_type_identity expected = {};
                result = decode_compressed_uint(data, size, offset, &encoded);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return false;
                }

                result = resolve_type_identity(assembly, decode_type_token(encoded), actual_element_type, &actual);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return false;
                }

                expected.element_type = bind_type->element_type;
                expected.type_namespace = bind_type->type_namespace;
                expected.type_name = bind_type->type_name;
                return type_identity_equals(&actual, &expected);
            }

            case ZACLR_ELEMENT_TYPE_PTR:
            case ZACLR_ELEMENT_TYPE_SZARRAY:
            case ZACLR_ELEMENT_TYPE_BYREF:
                return bind_type->child != NULL
                    && managed_type_matches_bind(assembly, data, size, offset, bind_type->child);

            case ZACLR_ELEMENT_TYPE_VAR:
            case ZACLR_ELEMENT_TYPE_MVAR:
                result = decode_compressed_uint(data, size, offset, &encoded);
                return result.status == ZACLR_STATUS_OK && encoded == bind_type->generic_index;

            case ZACLR_ELEMENT_TYPE_GENERICINST:
            {
                uint8_t owner_element_type;
                uint8_t owner_flags;
                uint32_t arg_count;
                uint32_t index;
                struct zaclr_resolved_type_identity actual = {};
                struct zaclr_resolved_type_identity expected = {};

                if (!parse_type_header(data, size, offset, &owner_element_type, &owner_flags)
                    || owner_flags != 0u
                    || (owner_element_type != ZACLR_ELEMENT_TYPE_CLASS && owner_element_type != ZACLR_ELEMENT_TYPE_VALUETYPE))
                {
                    return false;
                }

                result = decode_compressed_uint(data, size, offset, &encoded);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return false;
                }

                result = resolve_type_identity(assembly, decode_type_token(encoded), owner_element_type, &actual);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return false;
                }

                expected.element_type = owner_element_type;
                expected.type_namespace = bind_type->type_namespace;
                expected.type_name = bind_type->type_name;
                if (!type_identity_equals(&actual, &expected))
                {
                    return false;
                }

                result = decode_compressed_uint(data, size, offset, &arg_count);
                if (result.status != ZACLR_STATUS_OK || arg_count != bind_type->generic_arg_count)
                {
                    return false;
                }

                for (index = 0u; index < arg_count; ++index)
                {
                    if (bind_type->generic_args == NULL
                        || !managed_type_matches_bind(assembly, data, size, offset, &bind_type->generic_args[index]))
                    {
                        return false;
                    }
                }

                return true;
            }

            default:
                return true;
        }
    }

    static void signature_type_to_bind_type(const struct zaclr_loaded_assembly* assembly,
                                            const struct zaclr_signature_type* signature_type,
                                            struct zaclr_native_bind_sig_type* bind_type)
    {
        struct zaclr_resolved_type_identity resolved = {};

        if (bind_type == NULL)
        {
            return;
        }

        *bind_type = {};
        if (signature_type == NULL)
        {
            return;
        }

        bind_type->element_type = signature_type->element_type;
        bind_type->flags = (uint8_t)signature_type->flags;
        bind_type->generic_index = (uint16_t)signature_type->generic_param_index;

        if (assembly != NULL
            && (signature_type->element_type == ZACLR_ELEMENT_TYPE_CLASS
                || signature_type->element_type == ZACLR_ELEMENT_TYPE_VALUETYPE)
            && !zaclr_token_is_nil(&signature_type->type_token)
            && resolve_type_identity(assembly,
                                     signature_type->type_token,
                                     signature_type->element_type,
                                     &resolved).status == ZACLR_STATUS_OK)
        {
            bind_type->type_namespace = resolved.type_namespace;
            bind_type->type_name = resolved.type_name;
        }
    }

    static bool signatures_equal_core(const struct zaclr_loaded_assembly* left_assembly,
                                      const struct zaclr_signature_desc* left_signature,
                                      const struct zaclr_loaded_assembly* right_assembly,
                                      const struct zaclr_signature_desc* right_signature)
    {
        uint32_t left_offset = 1u;
        uint32_t right_offset = 1u;
        uint32_t ignored_left;
        uint32_t ignored_right;
        uint32_t index;
        struct zaclr_native_bind_sig_type bind_type = {};

        if (left_assembly == NULL || left_signature == NULL || right_assembly == NULL || right_signature == NULL)
        {
            return false;
        }

        if ((((left_signature->calling_convention & 0x20u) == 0u) ? 1u : 0u)
                != (((right_signature->calling_convention & 0x20u) == 0u) ? 1u : 0u)
            || left_signature->parameter_count != right_signature->parameter_count
            || left_signature->generic_parameter_count != right_signature->generic_parameter_count)
        {
            return false;
        }

        if ((left_signature->calling_convention & 0x10u) != 0u)
        {
            if (decode_compressed_uint(left_signature->blob.data, left_signature->blob.size, &left_offset, &ignored_left).status != ZACLR_STATUS_OK
                || decode_compressed_uint(right_signature->blob.data, right_signature->blob.size, &right_offset, &ignored_right).status != ZACLR_STATUS_OK
                || ignored_left != ignored_right)
            {
                return false;
            }
        }

        if (decode_compressed_uint(left_signature->blob.data, left_signature->blob.size, &left_offset, &ignored_left).status != ZACLR_STATUS_OK
            || decode_compressed_uint(right_signature->blob.data, right_signature->blob.size, &right_offset, &ignored_right).status != ZACLR_STATUS_OK
            || ignored_left != ignored_right)
        {
            return false;
        }

        signature_type_to_bind_type(right_assembly, &right_signature->return_type, &bind_type);
        if (!managed_type_matches_bind(left_assembly,
                                       left_signature->blob.data,
                                       left_signature->blob.size,
                                       &left_offset,
                                       &bind_type))
        {
            return false;
        }

        for (index = 0u; index < left_signature->parameter_count; ++index)
        {
            struct zaclr_signature_type parameter_type = {};
            if (zaclr_signature_read_method_parameter(right_signature, index, &parameter_type).status != ZACLR_STATUS_OK)
            {
                return false;
            }

            signature_type_to_bind_type(right_assembly, &parameter_type, &bind_type);
            if (!managed_type_matches_bind(left_assembly,
                                           left_signature->blob.data,
                                           left_signature->blob.size,
                                           &left_offset,
                                           &bind_type))
            {
                return false;
            }
        }

        (void)right_assembly;
        return true;
    }
}

extern "C" const char* zaclr_native_assembly_name(const struct zaclr_native_assembly_descriptor* assembly)
{
    return assembly != NULL ? assembly->assembly_name : NULL;
}

extern "C" const struct zaclr_native_bind_method* zaclr_native_assembly_method_lookup(
    const struct zaclr_native_assembly_descriptor* assembly)
{
    return assembly != NULL ? assembly->method_lookup : NULL;
}

extern "C" uint32_t zaclr_native_assembly_method_count(const struct zaclr_native_assembly_descriptor* assembly)
{
    return assembly != NULL ? assembly->method_count : 0u;
}

extern "C" bool zaclr_native_bind_method_matches_managed(const struct zaclr_loaded_assembly* assembly,
                                                          const struct zaclr_type_desc* owning_type,
                                                          const struct zaclr_method_desc* method,
                                                          const struct zaclr_native_bind_method* candidate)
{
    uint32_t offset = 1u;
    uint32_t ignored;
    uint32_t parameter_index;

    if (assembly == NULL || owning_type == NULL || method == NULL || candidate == NULL)
    {
        return false;
    }

    if (!zaclr_internal_call_text_equals(candidate->type_namespace, owning_type->type_namespace.text)
        || !zaclr_internal_call_text_equals(candidate->type_name, owning_type->type_name.text)
        || !zaclr_internal_call_text_equals(candidate->method_name, method->name.text)
        || candidate->signature.has_this != (((method->signature.calling_convention & 0x20u) != 0u) ? 1u : 0u)
        || candidate->signature.parameter_count != method->signature.parameter_count)
    {
        return false;
    }

    if ((method->signature.calling_convention & 0x10u) != 0u)
    {
        if (decode_compressed_uint(method->signature.blob.data, method->signature.blob.size, &offset, &ignored).status != ZACLR_STATUS_OK)
        {
            return false;
        }
    }

    if (decode_compressed_uint(method->signature.blob.data, method->signature.blob.size, &offset, &ignored).status != ZACLR_STATUS_OK)
    {
        return false;
    }

    if (!managed_type_matches_bind(assembly,
                                   method->signature.blob.data,
                                   method->signature.blob.size,
                                   &offset,
                                   &candidate->signature.return_type))
    {
        return false;
    }

    for (parameter_index = 0u; parameter_index < method->signature.parameter_count; ++parameter_index)
    {
        if (candidate->signature.parameter_types == NULL
            || !managed_type_matches_bind(assembly,
                                          method->signature.blob.data,
                                          method->signature.blob.size,
                                          &offset,
                                          &candidate->signature.parameter_types[parameter_index]))
        {
            return false;
        }
    }

    return true;
}

extern "C" bool zaclr_managed_signatures_equal(const struct zaclr_loaded_assembly* left_assembly,
                                                const struct zaclr_signature_desc* left_signature,
                                                const struct zaclr_loaded_assembly* right_assembly,
                                                const struct zaclr_signature_desc* right_signature)
{
    return signatures_equal_core(left_assembly, left_signature, right_assembly, right_signature)
        && signatures_equal_core(right_assembly, right_signature, left_assembly, left_signature);
}
