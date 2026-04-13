#include <kernel/zaclr/typesystem/zaclr_generic_context.h>

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/include/zaclr_public_api.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

namespace
{
    static struct zaclr_result invalid_argument(void)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    static struct zaclr_result bad_metadata(void)
    {
        return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_METADATA);
    }

    static struct zaclr_result unsupported(void)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_METADATA);
    }

    static struct zaclr_result allocate_arguments(uint32_t count,
                                                  struct zaclr_generic_argument** out_arguments)
    {
        struct zaclr_generic_argument* arguments;

        if (out_arguments == NULL)
        {
            return invalid_argument();
        }

        *out_arguments = NULL;
        if (count == 0u)
        {
            return zaclr_result_ok();
        }

        arguments = (struct zaclr_generic_argument*)kernel_alloc(sizeof(struct zaclr_generic_argument) * count);
        if (arguments == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_METADATA);
        }

        kernel_memset(arguments, 0, sizeof(struct zaclr_generic_argument) * count);
        *out_arguments = arguments;
        return zaclr_result_ok();
    }

    static void reset_argument(struct zaclr_generic_argument* argument)
    {
        uint32_t index;

        if (argument == NULL)
        {
            return;
        }

        if (argument->instantiation_args != NULL)
        {
            for (index = 0u; index < argument->instantiation_arg_count; ++index)
            {
                reset_argument(&argument->instantiation_args[index]);
            }

            kernel_free(argument->instantiation_args);
        }

        *argument = {};
    }

    static struct zaclr_result clone_argument(struct zaclr_generic_argument* destination,
                                              const struct zaclr_generic_argument* source)
    {
        uint32_t index;
        struct zaclr_result result;

        if (destination == NULL || source == NULL)
        {
            return invalid_argument();
        }

        *destination = *source;
        destination->instantiation_args = NULL;

        if (source->instantiation_arg_count == 0u || source->instantiation_args == NULL)
        {
            return zaclr_result_ok();
        }

        result = allocate_arguments(source->instantiation_arg_count, &destination->instantiation_args);
        if (result.status != ZACLR_STATUS_OK)
        {
            *destination = {};
            return result;
        }

        for (index = 0u; index < source->instantiation_arg_count; ++index)
        {
            result = clone_argument(&destination->instantiation_args[index], &source->instantiation_args[index]);
            if (result.status != ZACLR_STATUS_OK)
            {
                for (uint32_t cleanup_index = 0u; cleanup_index < index; ++cleanup_index)
                {
                    reset_argument(&destination->instantiation_args[cleanup_index]);
                }

                kernel_free(destination->instantiation_args);
                *destination = {};
                return result;
            }
        }

        return zaclr_result_ok();
    }

    static struct zaclr_result decode_compressed_uint(const struct zaclr_slice* blob,
                                                      uint32_t* offset,
                                                      uint32_t* value)
    {
        uint8_t first;

        if (blob == NULL || blob->data == NULL || offset == NULL || value == NULL || *offset >= blob->size)
        {
            return bad_metadata();
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
                return bad_metadata();
            }

            *value = (((uint32_t)(first & 0x3Fu)) << 8) | (uint32_t)blob->data[*offset + 1u];
            *offset += 2u;
            return zaclr_result_ok();
        }

        if ((first & 0xE0u) != 0xC0u || (*offset + 3u) >= blob->size)
        {
            return bad_metadata();
        }

        *value = (((uint32_t)(first & 0x1Fu)) << 24)
               | ((uint32_t)blob->data[*offset + 1u] << 16)
               | ((uint32_t)blob->data[*offset + 2u] << 8)
               | (uint32_t)blob->data[*offset + 3u];
        *offset += 4u;
        return zaclr_result_ok();
    }

    static struct zaclr_result decode_type_def_or_ref_token(const struct zaclr_slice* blob,
                                                            uint32_t* offset,
                                                            struct zaclr_token* out_token)
    {
        uint32_t coded_value;
        uint32_t tag;
        uint32_t table;
        struct zaclr_result result;

        if (out_token == NULL)
        {
            return invalid_argument();
        }

        result = decode_compressed_uint(blob, offset, &coded_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        tag = coded_value & 0x3u;
        if (tag == 0u)
        {
            table = ZACLR_TOKEN_TABLE_TYPEDEF;
        }
        else if (tag == 1u)
        {
            table = ZACLR_TOKEN_TABLE_TYPEREF;
        }
        else if (tag == 2u)
        {
            table = ZACLR_TOKEN_TABLE_TYPESPEC;
        }
        else
        {
            return unsupported();
        }

        *out_token = zaclr_token_make((table << 24) | (coded_value >> 2u));
        return zaclr_result_ok();
    }

    static struct zaclr_result resolve_argument_assembly(const struct zaclr_loaded_assembly* current_assembly,
                                                         struct zaclr_runtime* runtime,
                                                         struct zaclr_token token,
                                                         const struct zaclr_loaded_assembly** out_assembly)
    {
        const struct zaclr_loaded_assembly* resolved_assembly = NULL;
        const struct zaclr_type_desc* resolved_type = NULL;
        struct zaclr_result result;

        if (out_assembly == NULL)
        {
            return invalid_argument();
        }

        *out_assembly = current_assembly;
        if (current_assembly == NULL || runtime == NULL)
        {
            return zaclr_result_ok();
        }

        result = zaclr_type_system_resolve_type_desc(current_assembly,
                                                     runtime,
                                                     token,
                                                     &resolved_assembly,
                                                     &resolved_type);
        if (result.status == ZACLR_STATUS_OK && resolved_assembly != NULL)
        {
            *out_assembly = resolved_assembly;
            return zaclr_result_ok();
        }

        if (result.status == ZACLR_STATUS_NOT_IMPLEMENTED || result.status == ZACLR_STATUS_NOT_FOUND)
        {
            return zaclr_result_ok();
        }

        return result;
    }

    static struct zaclr_result decode_instantiation_argument(const struct zaclr_loaded_assembly* current_assembly,
                                                             struct zaclr_runtime* runtime,
                                                             const struct zaclr_slice* blob,
                                                             uint32_t* offset,
                                                             struct zaclr_generic_argument* out_argument)
    {
        uint8_t element_type;
        struct zaclr_result result;

        if (blob == NULL || offset == NULL || out_argument == NULL || *offset >= blob->size)
        {
            return invalid_argument();
        }

        *out_argument = {};
        element_type = blob->data[*offset];
        *offset += 1u;

        switch (element_type)
        {
            case ZACLR_ELEMENT_TYPE_BOOLEAN:
            case ZACLR_ELEMENT_TYPE_CHAR:
            case ZACLR_ELEMENT_TYPE_I1:
            case ZACLR_ELEMENT_TYPE_U1:
            case ZACLR_ELEMENT_TYPE_I2:
            case ZACLR_ELEMENT_TYPE_U2:
            case ZACLR_ELEMENT_TYPE_I4:
            case ZACLR_ELEMENT_TYPE_U4:
            case ZACLR_ELEMENT_TYPE_I8:
            case ZACLR_ELEMENT_TYPE_U8:
            case ZACLR_ELEMENT_TYPE_R4:
            case ZACLR_ELEMENT_TYPE_R8:
            case ZACLR_ELEMENT_TYPE_STRING:
            case ZACLR_ELEMENT_TYPE_OBJECT:
            case ZACLR_ELEMENT_TYPE_I:
            case ZACLR_ELEMENT_TYPE_U:
                out_argument->kind = ZACLR_GENERIC_ARGUMENT_KIND_PRIMITIVE;
                out_argument->element_type = element_type;
                out_argument->token = zaclr_token_make(0u);
                out_argument->assembly = current_assembly;
                return zaclr_result_ok();

            case ZACLR_ELEMENT_TYPE_CLASS:
            case ZACLR_ELEMENT_TYPE_VALUETYPE:
                out_argument->kind = ZACLR_GENERIC_ARGUMENT_KIND_CONCRETE_TYPE;
                out_argument->element_type = element_type;
                result = decode_type_def_or_ref_token(blob, offset, &out_argument->token);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                return resolve_argument_assembly(current_assembly,
                                                 runtime,
                                                 out_argument->token,
                                                 &out_argument->assembly);

            case ZACLR_ELEMENT_TYPE_GENERICINST:
            {
                uint8_t owner_element_type;
                uint32_t argument_count;
                uint32_t argument_index;

                if (*offset >= blob->size)
                {
                    return bad_metadata();
                }

                owner_element_type = blob->data[*offset];
                if (owner_element_type != ZACLR_ELEMENT_TYPE_CLASS
                    && owner_element_type != ZACLR_ELEMENT_TYPE_VALUETYPE)
                {
                    return unsupported();
                }

                *offset += 1u;
                out_argument->kind = ZACLR_GENERIC_ARGUMENT_KIND_GENERIC_INST;
                out_argument->element_type = owner_element_type;
                result = decode_type_def_or_ref_token(blob, offset, &out_argument->token);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = resolve_argument_assembly(current_assembly,
                                                   runtime,
                                                   out_argument->token,
                                                   &out_argument->assembly);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = decode_compressed_uint(blob, offset, &argument_count);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                out_argument->instantiation_arg_count = argument_count;
                result = allocate_arguments(argument_count, &out_argument->instantiation_args);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                for (argument_index = 0u; argument_index < argument_count; ++argument_index)
                {
                    result = decode_instantiation_argument(current_assembly,
                                                           runtime,
                                                           blob,
                                                           offset,
                                                           &out_argument->instantiation_args[argument_index]);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        reset_argument(out_argument);
                        return result;
                    }
                }

                return zaclr_result_ok();
            }

            case ZACLR_ELEMENT_TYPE_VAR:
            case ZACLR_ELEMENT_TYPE_MVAR:
            {
                uint32_t generic_index = 0u;
                result = decode_compressed_uint(blob, offset, &generic_index);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                out_argument->kind = element_type == ZACLR_ELEMENT_TYPE_VAR
                    ? ZACLR_GENERIC_ARGUMENT_KIND_TYPE_VAR
                    : ZACLR_GENERIC_ARGUMENT_KIND_METHOD_VAR;
                out_argument->element_type = element_type;
                out_argument->token = zaclr_token_make(0u);
                out_argument->assembly = current_assembly;
                out_argument->generic_param_index = generic_index;
                return zaclr_result_ok();
            }

            default:
                return unsupported();
        }
    }

    static struct zaclr_result parse_instantiation_arguments(struct zaclr_generic_argument** out_arguments,
                                                             uint32_t* out_count,
                                                             const struct zaclr_loaded_assembly* current_assembly,
                                                             struct zaclr_runtime* runtime,
                                                             const struct zaclr_slice* instantiation_blob)
    {
        uint32_t offset = 0u;
        uint32_t argument_count;
        uint32_t argument_index;
        struct zaclr_generic_argument* arguments = NULL;
        struct zaclr_result result;

        if (out_arguments == NULL || out_count == NULL)
        {
            return invalid_argument();
        }

        *out_arguments = NULL;
        *out_count = 0u;
        if (instantiation_blob == NULL || instantiation_blob->data == NULL || instantiation_blob->size == 0u)
        {
            return zaclr_result_ok();
        }

        if (instantiation_blob->data[offset++] != 0x0Au)
        {
            return bad_metadata();
        }

        result = decode_compressed_uint(instantiation_blob, &offset, &argument_count);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = allocate_arguments(argument_count, &arguments);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        for (argument_index = 0u; argument_index < argument_count; ++argument_index)
        {
            result = decode_instantiation_argument(current_assembly,
                                                   runtime,
                                                   instantiation_blob,
                                                   &offset,
                                                   &arguments[argument_index]);
            if (result.status != ZACLR_STATUS_OK)
            {
                kernel_free(arguments);
                return result;
            }
        }

        *out_arguments = arguments;
        *out_count = argument_count;
        return zaclr_result_ok();
    }

    static struct zaclr_result set_instantiation(struct zaclr_generic_argument** target_arguments,
                                                 uint32_t* target_count,
                                                 const struct zaclr_loaded_assembly* current_assembly,
                                                 struct zaclr_runtime* runtime,
                                                 const struct zaclr_slice* instantiation_blob)
    {
        struct zaclr_generic_argument* arguments = NULL;
        uint32_t count = 0u;
        struct zaclr_result result;

        result = parse_instantiation_arguments(&arguments,
                                               &count,
                                               current_assembly,
                                               runtime,
                                               instantiation_blob);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (*target_arguments != NULL)
        {
            kernel_free(*target_arguments);
        }

        *target_arguments = arguments;
        *target_count = count;
        return zaclr_result_ok();
    }
}

extern "C" void zaclr_generic_context_reset(struct zaclr_generic_context* context)
{
    if (context == NULL)
    {
        return;
    }

    if (context->type_args != NULL)
    {
        for (uint32_t index = 0u; index < context->type_arg_count; ++index)
        {
            reset_argument(&context->type_args[index]);
        }
        kernel_free(context->type_args);
    }

    if (context->method_args != NULL)
    {
        for (uint32_t index = 0u; index < context->method_arg_count; ++index)
        {
            reset_argument(&context->method_args[index]);
        }
        kernel_free(context->method_args);
    }

    *context = {};
}

extern "C" struct zaclr_result zaclr_generic_context_clone(struct zaclr_generic_context* destination,
                                                             const struct zaclr_generic_context* source)
{
    struct zaclr_result result;

    if (destination == NULL)
    {
        return invalid_argument();
    }

    zaclr_generic_context_reset(destination);
    if (source == NULL)
    {
        return zaclr_result_ok();
    }

    result = allocate_arguments(source->type_arg_count, &destination->type_args);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (source->type_arg_count != 0u)
    {
        for (uint32_t index = 0u; index < source->type_arg_count; ++index)
        {
            result = clone_argument(&destination->type_args[index], &source->type_args[index]);
            if (result.status != ZACLR_STATUS_OK)
            {
                zaclr_generic_context_reset(destination);
                return result;
            }
        }
    }
    destination->type_arg_count = source->type_arg_count;

    result = allocate_arguments(source->method_arg_count, &destination->method_args);
    if (result.status != ZACLR_STATUS_OK)
    {
        zaclr_generic_context_reset(destination);
        return result;
    }

    if (source->method_arg_count != 0u)
    {
        for (uint32_t index = 0u; index < source->method_arg_count; ++index)
        {
            result = clone_argument(&destination->method_args[index], &source->method_args[index]);
            if (result.status != ZACLR_STATUS_OK)
            {
                zaclr_generic_context_reset(destination);
                return result;
            }
        }
    }
    destination->method_arg_count = source->method_arg_count;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_generic_context_set_method_instantiation(struct zaclr_generic_context* context,
                                                                                const struct zaclr_loaded_assembly* current_assembly,
                                                                                struct zaclr_runtime* runtime,
                                                                                const struct zaclr_slice* instantiation_blob)
{
    if (context == NULL)
    {
        return invalid_argument();
    }

    return set_instantiation(&context->method_args,
                             &context->method_arg_count,
                             current_assembly,
                             runtime,
                             instantiation_blob);
}

extern "C" struct zaclr_result zaclr_generic_context_set_type_instantiation(struct zaclr_generic_context* context,
                                                                               const struct zaclr_loaded_assembly* current_assembly,
                                                                               struct zaclr_runtime* runtime,
                                                                               const struct zaclr_slice* instantiation_blob)
{
    if (context == NULL)
    {
        return invalid_argument();
    }

    return set_instantiation(&context->type_args,
                             &context->type_arg_count,
                             current_assembly,
                             runtime,
                             instantiation_blob);
}

extern "C" const struct zaclr_generic_argument* zaclr_generic_context_get_type_argument(const struct zaclr_generic_context* context,
                                                                                          uint32_t index)
{
    if (context == NULL || context->type_args == NULL || index >= context->type_arg_count)
    {
        return NULL;
    }

    return &context->type_args[index];
}

extern "C" const struct zaclr_generic_argument* zaclr_generic_context_get_method_argument(const struct zaclr_generic_context* context,
                                                                                            uint32_t index)
{
    if (context == NULL || context->method_args == NULL || index >= context->method_arg_count)
    {
        return NULL;
    }

    return &context->method_args[index];
}

extern "C" struct zaclr_result zaclr_generic_context_resolve_signature_type(const struct zaclr_generic_context* context,
                                                                              const struct zaclr_loaded_assembly* current_assembly,
                                                                              struct zaclr_runtime* runtime,
                                                                              const struct zaclr_signature_type* signature_type,
                                                                              struct zaclr_generic_argument* out_argument)
{
    struct zaclr_result result;

    if (signature_type == NULL || out_argument == NULL)
    {
        return invalid_argument();
    }

    *out_argument = {};
    switch (signature_type->element_type)
    {
        case ZACLR_ELEMENT_TYPE_VAR:
        {
            const struct zaclr_generic_argument* argument = zaclr_generic_context_get_type_argument(context,
                                                                                                    signature_type->generic_param_index);
            if (argument == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
            }

            return clone_argument(out_argument, argument);
        }

        case ZACLR_ELEMENT_TYPE_MVAR:
        {
            const struct zaclr_generic_argument* argument = zaclr_generic_context_get_method_argument(context,
                                                                                                      signature_type->generic_param_index);
            if (argument == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
            }

            return clone_argument(out_argument, argument);
        }

        case ZACLR_ELEMENT_TYPE_CLASS:
        case ZACLR_ELEMENT_TYPE_VALUETYPE:
            out_argument->kind = ZACLR_GENERIC_ARGUMENT_KIND_CONCRETE_TYPE;
            out_argument->element_type = signature_type->element_type;
            out_argument->token = signature_type->type_token;
            out_argument->assembly = current_assembly;
            if (!zaclr_token_is_nil(&signature_type->type_token))
            {
                result = resolve_argument_assembly(current_assembly,
                                                   runtime,
                                                   signature_type->type_token,
                                                   &out_argument->assembly);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
            }
            return zaclr_result_ok();

        case ZACLR_ELEMENT_TYPE_BOOLEAN:
        case ZACLR_ELEMENT_TYPE_CHAR:
        case ZACLR_ELEMENT_TYPE_I1:
        case ZACLR_ELEMENT_TYPE_U1:
        case ZACLR_ELEMENT_TYPE_I2:
        case ZACLR_ELEMENT_TYPE_U2:
        case ZACLR_ELEMENT_TYPE_I4:
        case ZACLR_ELEMENT_TYPE_U4:
        case ZACLR_ELEMENT_TYPE_I8:
        case ZACLR_ELEMENT_TYPE_U8:
        case ZACLR_ELEMENT_TYPE_R4:
        case ZACLR_ELEMENT_TYPE_R8:
        case ZACLR_ELEMENT_TYPE_STRING:
        case ZACLR_ELEMENT_TYPE_OBJECT:
        case ZACLR_ELEMENT_TYPE_I:
        case ZACLR_ELEMENT_TYPE_U:
            out_argument->kind = ZACLR_GENERIC_ARGUMENT_KIND_PRIMITIVE;
            out_argument->element_type = signature_type->element_type;
            out_argument->assembly = current_assembly;
            return zaclr_result_ok();

        default:
            return unsupported();
    }
}
