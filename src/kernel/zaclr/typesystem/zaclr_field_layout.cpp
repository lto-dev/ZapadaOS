#include <kernel/zaclr/typesystem/zaclr_field_layout.h>

#define ZACLR_FIELD_SIG_MARKER 0x06u

namespace
{
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

        if (blob == NULL || offset == NULL || value == NULL || *offset >= blob->size)
        {
            return bad_metadata();
        }

        data = blob->data;
        first = data[*offset];

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
                return bad_metadata();
            }

            *value = (((uint32_t)(first & 0x3Fu)) << 8)
                   | (uint32_t)data[*offset + 1u];
            *offset += 2u;
            return zaclr_result_ok();
        }

        if ((first & 0xE0u) == 0xC0u)
        {
            if ((*offset + 3u) >= blob->size)
            {
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
}

extern "C" uint32_t zaclr_field_layout_size_from_element_type(uint8_t element_type)
{
    switch (element_type)
    {
        case ZACLR_ELEMENT_TYPE_BOOLEAN:
        case ZACLR_ELEMENT_TYPE_I1:
        case ZACLR_ELEMENT_TYPE_U1:
            return 1u;

        case ZACLR_ELEMENT_TYPE_CHAR:
        case ZACLR_ELEMENT_TYPE_I2:
        case ZACLR_ELEMENT_TYPE_U2:
            return 2u;

        case ZACLR_ELEMENT_TYPE_I4:
        case ZACLR_ELEMENT_TYPE_U4:
        case ZACLR_ELEMENT_TYPE_R4:
            return 4u;

        case ZACLR_ELEMENT_TYPE_I8:
        case ZACLR_ELEMENT_TYPE_U8:
        case ZACLR_ELEMENT_TYPE_R8:
            return 8u;

        case ZACLR_ELEMENT_TYPE_I:
        case ZACLR_ELEMENT_TYPE_U:
            return 8u; /* 64-bit platform */

        case ZACLR_ELEMENT_TYPE_CLASS:
        case ZACLR_ELEMENT_TYPE_OBJECT:
        case ZACLR_ELEMENT_TYPE_STRING:
        case ZACLR_ELEMENT_TYPE_SZARRAY:
        case ZACLR_ELEMENT_TYPE_ARRAY:
        case ZACLR_ELEMENT_TYPE_PTR:
        case ZACLR_ELEMENT_TYPE_FNPTR:
            return 8u; /* pointer size on 64-bit */

        default:
            return 0u;
    }
}

extern "C" uint8_t zaclr_field_layout_is_reference(uint8_t element_type)
{
    switch (element_type)
    {
        case ZACLR_ELEMENT_TYPE_CLASS:
        case ZACLR_ELEMENT_TYPE_OBJECT:
        case ZACLR_ELEMENT_TYPE_STRING:
        case ZACLR_ELEMENT_TYPE_SZARRAY:
        case ZACLR_ELEMENT_TYPE_ARRAY:
            return 1u;

        default:
            return 0u;
    }
}

extern "C" uint32_t zaclr_field_layout_compute_alignment(uint8_t element_type)
{
    uint32_t size = zaclr_field_layout_size_from_element_type(element_type);

    if (size == 0u)
    {
        return 1u;
    }

    if (size > 8u)
    {
        return 8u;
    }

    return size;
}

extern "C" struct zaclr_result zaclr_field_layout_parse_field_signature(const struct zaclr_slice* blob,
                                                                        struct zaclr_signature_type* out_type)
{
    uint32_t offset;
    uint8_t marker;
    uint32_t value;
    uint32_t token_table;

    if (blob == NULL || out_type == NULL || blob->data == NULL || blob->size == 0u)
    {
        return invalid_argument();
    }

    *out_type = {};
    offset = 0u;

    /* First byte must be the FIELD calling convention marker (0x06) */
    marker = blob->data[offset];
    offset += 1u;

    if (marker != ZACLR_FIELD_SIG_MARKER)
    {
        return bad_metadata();
    }

    if (offset >= blob->size)
    {
        return bad_metadata();
    }

    /* Handle BYREF and PINNED modifiers */
    out_type->element_type = blob->data[offset];
    offset += 1u;

    while (out_type->element_type == ZACLR_ELEMENT_TYPE_BYREF || out_type->element_type == ZACLR_ELEMENT_TYPE_PINNED)
    {
        if (out_type->element_type == ZACLR_ELEMENT_TYPE_BYREF)
        {
            out_type->flags |= ZACLR_SIGNATURE_TYPE_FLAG_BYREF;
        }
        else
        {
            out_type->flags |= ZACLR_SIGNATURE_TYPE_FLAG_PINNED;
        }

        if (offset >= blob->size)
        {
            return bad_metadata();
        }

        out_type->element_type = blob->data[offset];
        offset += 1u;
    }

    /* Handle CMOD_OPT / CMOD_REQD prefixes */
    while (out_type->element_type == ZACLR_ELEMENT_TYPE_CMOD_OPT || out_type->element_type == ZACLR_ELEMENT_TYPE_CMOD_REQD)
    {
        struct zaclr_result result = decode_compressed_uint(blob, &offset, &value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (offset >= blob->size)
        {
            return bad_metadata();
        }

        out_type->element_type = blob->data[offset];
        offset += 1u;
    }

    /* Decode the type token for CLASS and VALUETYPE */
    switch (out_type->element_type)
    {
        case ZACLR_ELEMENT_TYPE_CLASS:
        case ZACLR_ELEMENT_TYPE_VALUETYPE:
        {
            struct zaclr_result result = decode_compressed_uint(blob, &offset, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            token_table = value & 0x3u;
            out_type->type_token = zaclr_token_make(
                ((token_table == 0u ? ZACLR_TOKEN_TABLE_TYPEDEF
                : (token_table == 1u ? ZACLR_TOKEN_TABLE_TYPEREF
                                     : ZACLR_TOKEN_TABLE_TYPESPEC))
                << 24)
                | (value >> 2));
            break;
        }

        case ZACLR_ELEMENT_TYPE_SZARRAY:
        case ZACLR_ELEMENT_TYPE_PTR:
            /* Skip the inner element type; we record the outer type only */
            break;

        case ZACLR_ELEMENT_TYPE_GENERICINST:
            /* For generic instances, the next byte is CLASS or VALUETYPE */
            if (offset < blob->size)
            {
                uint8_t inner = blob->data[offset];
                if (inner == ZACLR_ELEMENT_TYPE_CLASS || inner == ZACLR_ELEMENT_TYPE_VALUETYPE)
                {
                    offset += 1u;
                    struct zaclr_result result = decode_compressed_uint(blob, &offset, &value);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }

                    token_table = value & 0x3u;
                    out_type->type_token = zaclr_token_make(
                        ((token_table == 0u ? ZACLR_TOKEN_TABLE_TYPEDEF
                        : (token_table == 1u ? ZACLR_TOKEN_TABLE_TYPEREF
                                             : ZACLR_TOKEN_TABLE_TYPESPEC))
                        << 24)
                        | (value >> 2));
                }
            }
            break;

        default:
            break;
    }

    return zaclr_result_ok();
}
