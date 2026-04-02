#include <kernel/zaclr/metadata/zaclr_signature.h>

namespace {

static struct zaclr_result bad_metadata(void)
{
    return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_METADATA);
}

static struct zaclr_result invalid_argument(void)
{
    return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
}

static struct zaclr_result decode_compressed_uint(const struct zaclr_slice* blob,
                                                  uint32_t* offset,
                                                  uint32_t* value)
{
    const uint8_t* data;
    uint8_t first;

    if (blob == NULL || offset == NULL || value == NULL || *offset >= blob->size) {
        return bad_metadata();
    }

    data = blob->data;
    first = data[*offset];

    if ((first & 0x80u) == 0u) {
        *value = first;
        *offset += 1u;
        return zaclr_result_ok();
    }

    if ((first & 0xC0u) == 0x80u) {
        if ((*offset + 1u) >= blob->size) {
            return bad_metadata();
        }

        *value = (((uint32_t)(first & 0x3Fu)) << 8)
               | (uint32_t)data[*offset + 1u];
        *offset += 2u;
        return zaclr_result_ok();
    }

    if ((first & 0xE0u) == 0xC0u) {
        if ((*offset + 3u) >= blob->size) {
            return bad_metadata();
        }

        *value = (((uint32_t)(first & 0x1Fu)) << 24)
               | ((uint32_t)data[*offset + 1u] << 16)
               | ((uint32_t)data[*offset + 2u] << 8)
               | (uint32_t)data[*offset + 3u];
        *offset += 4u;
        return zaclr_result_ok();
    }

    return bad_metadata();
}

static struct zaclr_result skip_signature_type(const struct zaclr_slice* blob, uint32_t* offset);

static struct zaclr_result decode_signature_type(const struct zaclr_slice* blob,
                                                 uint32_t* offset,
                                                 struct zaclr_signature_type* type)
{
    uint32_t value;
    uint32_t token_table;
    struct zaclr_result result;

    if (blob == NULL || offset == NULL || type == NULL || *offset >= blob->size) {
        return invalid_argument();
    }

    *type = {};
    type->element_type = blob->data[*offset];
    *offset += 1u;

    while (type->element_type == ZACLR_ELEMENT_TYPE_BYREF || type->element_type == ZACLR_ELEMENT_TYPE_PINNED) {
        if (type->element_type == ZACLR_ELEMENT_TYPE_BYREF) {
            type->flags |= ZACLR_SIGNATURE_TYPE_FLAG_BYREF;
        } else {
            type->flags |= ZACLR_SIGNATURE_TYPE_FLAG_PINNED;
        }

        if (*offset >= blob->size) {
            return bad_metadata();
        }

        type->element_type = blob->data[*offset];
        *offset += 1u;
    }

    switch (type->element_type) {
        case ZACLR_ELEMENT_TYPE_CLASS:
        case ZACLR_ELEMENT_TYPE_VALUETYPE:
            result = decode_compressed_uint(blob, offset, &value);
            if (result.status != ZACLR_STATUS_OK) {
                return result;
            }

            token_table = value & 0x3u;
            type->type_token = zaclr_token_make(((token_table == 0u ? ZACLR_TOKEN_TABLE_TYPEDEF
                                                : (token_table == 1u ? ZACLR_TOKEN_TABLE_TYPEREF
                                                                     : ZACLR_TOKEN_TABLE_TYPESPEC))
                                                << 24)
                                               | (value >> 2));
            return zaclr_result_ok();

        case ZACLR_ELEMENT_TYPE_VAR:
        case ZACLR_ELEMENT_TYPE_MVAR:
            return decode_compressed_uint(blob, offset, &type->generic_param_index);

        case ZACLR_ELEMENT_TYPE_PTR:
        case ZACLR_ELEMENT_TYPE_SZARRAY:
            return skip_signature_type(blob, offset);

        case ZACLR_ELEMENT_TYPE_ARRAY: {
            uint32_t rank;
            uint32_t count;
            uint32_t index;

            result = skip_signature_type(blob, offset);
            if (result.status != ZACLR_STATUS_OK) {
                return result;
            }

            result = decode_compressed_uint(blob, offset, &rank);
            if (result.status != ZACLR_STATUS_OK) {
                return result;
            }

            result = decode_compressed_uint(blob, offset, &count);
            if (result.status != ZACLR_STATUS_OK) {
                return result;
            }

            for (index = 0u; index < count; ++index) {
                result = decode_compressed_uint(blob, offset, &value);
                if (result.status != ZACLR_STATUS_OK) {
                    return result;
                }
            }

            result = decode_compressed_uint(blob, offset, &count);
            if (result.status != ZACLR_STATUS_OK) {
                return result;
            }

            for (index = 0u; index < count; ++index) {
                result = decode_compressed_uint(blob, offset, &value);
                if (result.status != ZACLR_STATUS_OK) {
                    return result;
                }
            }

            (void)rank;
            return zaclr_result_ok();
        }

        case ZACLR_ELEMENT_TYPE_GENERICINST: {
            uint32_t arg_count;
            uint32_t index;

            result = decode_signature_type(blob, offset, type);
            if (result.status != ZACLR_STATUS_OK) {
                return result;
            }

            result = decode_compressed_uint(blob, offset, &arg_count);
            if (result.status != ZACLR_STATUS_OK) {
                return result;
            }

            for (index = 0u; index < arg_count; ++index) {
                result = skip_signature_type(blob, offset);
                if (result.status != ZACLR_STATUS_OK) {
                    return result;
                }
            }

            type->element_type = ZACLR_ELEMENT_TYPE_GENERICINST;
            return zaclr_result_ok();
        }

        case ZACLR_ELEMENT_TYPE_FNPTR: {
            struct zaclr_signature_desc nested;
            struct zaclr_slice nested_blob;

            nested_blob.data = blob->data + *offset;
            nested_blob.size = blob->size - *offset;
            result = zaclr_signature_parse_method(&nested_blob, &nested);
            if (result.status != ZACLR_STATUS_OK) {
                return result;
            }

            *offset += nested.header_size;
            return zaclr_result_ok();
        }

        default:
            return zaclr_result_ok();
    }
}

static struct zaclr_result skip_signature_type(const struct zaclr_slice* blob, uint32_t* offset)
{
    struct zaclr_signature_type type;
    return decode_signature_type(blob, offset, &type);
}

}

extern "C" uint32_t zaclr_signature_flags(const struct zaclr_signature_desc* signature)
{
    return signature != NULL ? signature->flags : 0u;
}

extern "C" uint64_t zaclr_signature_hash_text(const char* text)
{
    const uint64_t k_offset_basis = 14695981039346656037ull;
    const uint64_t k_prime = 1099511628211ull;
    uint64_t hash = k_offset_basis;

    if (text == NULL)
    {
        return 0u;
    }

    while (*text != '\0')
    {
        hash ^= (uint8_t)*text;
        hash *= k_prime;
        ++text;
    }

    return hash;
}

extern "C" struct zaclr_result zaclr_signature_parse_method(const struct zaclr_slice* blob,
                                                             struct zaclr_signature_desc* signature)
{
    uint32_t offset;
    struct zaclr_result result;

    if (blob == NULL || signature == NULL || blob->data == NULL || blob->size == 0u) {
        return invalid_argument();
    }

    *signature = {};
    signature->blob = *blob;
    signature->flags = ZACLR_SIGNATURE_FLAG_METHOD;

    offset = 0u;
    signature->calling_convention = blob->data[offset++];
    if ((signature->calling_convention & 0x10u) != 0u) {
        uint32_t generic_parameter_count = 0u;
        result = decode_compressed_uint(blob, &offset, &generic_parameter_count);
        if (result.status != ZACLR_STATUS_OK) {
            return result;
        }
        signature->generic_parameter_count = (uint16_t)generic_parameter_count;
    }

    {
        uint32_t parameter_count = 0u;
        result = decode_compressed_uint(blob, &offset, &parameter_count);
        if (result.status != ZACLR_STATUS_OK) {
            return result;
        }
        signature->parameter_count = (uint16_t)parameter_count;
    }

    result = decode_signature_type(blob, &offset, &signature->return_type);
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    signature->parameter_types_offset = offset;

    {
        uint32_t index;
        for (index = 0u; index < signature->parameter_count; ++index) {
            result = skip_signature_type(blob, &offset);
            if (result.status != ZACLR_STATUS_OK) {
                return result;
            }
        }
    }

    signature->header_size = (uint8_t)offset;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_signature_parse_generic_instantiation(const struct zaclr_slice* blob,
                                                                            struct zaclr_generic_instantiation_desc* instantiation)
{
    uint32_t offset;
    uint32_t argument_count;
    uint32_t index;
    struct zaclr_result result;

    if (blob == NULL || instantiation == NULL || blob->data == NULL || blob->size == 0u) {
        return invalid_argument();
    }

    *instantiation = {};
    instantiation->blob = *blob;

    offset = 0u;
    instantiation->calling_convention = blob->data[offset++];
    if (instantiation->calling_convention != 0x0Au) {
        return bad_metadata();
    }

    result = decode_compressed_uint(blob, &offset, &argument_count);
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    for (index = 0u; index < argument_count; ++index) {
        result = skip_signature_type(blob, &offset);
        if (result.status != ZACLR_STATUS_OK) {
            return result;
        }
    }

    instantiation->argument_count = (uint16_t)argument_count;
    instantiation->header_size = (uint8_t)offset;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_signature_read_method_parameter(const struct zaclr_signature_desc* signature,
                                                                       uint32_t parameter_index,
                                                                       struct zaclr_signature_type* parameter_type)
{
    uint32_t offset;
    uint32_t index;
    struct zaclr_result result;

    if (signature == NULL || parameter_type == NULL) {
        return invalid_argument();
    }

    if (parameter_index >= signature->parameter_count) {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    offset = signature->parameter_types_offset;
    for (index = 0u; index < parameter_index; ++index) {
        result = skip_signature_type(&signature->blob, &offset);
        if (result.status != ZACLR_STATUS_OK) {
            return result;
        }
    }

    return decode_signature_type(&signature->blob, &offset, parameter_type);
}
