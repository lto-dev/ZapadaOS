#include <kernel/zaclr/typesystem/zaclr_type_identity.h>

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

namespace
{
    static struct zaclr_result invalid_argument(void)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    static struct zaclr_result out_of_memory(void)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_METADATA);
    }

    static struct zaclr_result not_found(void)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    static struct zaclr_result allocate_identities(uint32_t count,
                                                   struct zaclr_type_identity** out_identities)
    {
        if (out_identities == NULL)
        {
            return invalid_argument();
        }

        *out_identities = NULL;
        if (count == 0u)
        {
            return zaclr_result_ok();
        }

        *out_identities = (struct zaclr_type_identity*)kernel_alloc(sizeof(struct zaclr_type_identity) * count);
        if (*out_identities == NULL)
        {
            return out_of_memory();
        }

        kernel_memset(*out_identities, 0, sizeof(struct zaclr_type_identity) * count);
        return zaclr_result_ok();
    }

    static void reset_generic_argument(struct zaclr_generic_argument* argument)
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
                reset_generic_argument(&argument->instantiation_args[index]);
            }

            kernel_free(argument->instantiation_args);
        }

        *argument = {};
    }
}

extern "C" void zaclr_type_identity_reset(struct zaclr_type_identity* identity)
{
    uint32_t index;

    if (identity == NULL)
    {
        return;
    }

    if (identity->generic_args != NULL)
    {
        for (index = 0u; index < identity->generic_arg_count; ++index)
        {
            zaclr_type_identity_reset(&identity->generic_args[index]);
        }

        kernel_free(identity->generic_args);
    }

    *identity = {};
}

extern "C" struct zaclr_result zaclr_type_identity_clone(struct zaclr_type_identity* destination,
                                                          const struct zaclr_type_identity* source)
{
    uint32_t index;
    struct zaclr_result result;

    if (destination == NULL || source == NULL)
    {
        return invalid_argument();
    }

    *destination = *source;
    destination->generic_args = NULL;

    if (source->generic_arg_count == 0u || source->generic_args == NULL)
    {
        return zaclr_result_ok();
    }

    result = allocate_identities(source->generic_arg_count, &destination->generic_args);
    if (result.status != ZACLR_STATUS_OK)
    {
        *destination = {};
        return result;
    }

    for (index = 0u; index < source->generic_arg_count; ++index)
    {
        result = zaclr_type_identity_clone(&destination->generic_args[index], &source->generic_args[index]);
        if (result.status != ZACLR_STATUS_OK)
        {
            zaclr_type_identity_reset(destination);
            return result;
        }
    }

    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_type_identity_from_generic_argument(const struct zaclr_generic_argument* argument,
                                                                          struct zaclr_type_identity* out_identity)
{
    uint32_t index;
    struct zaclr_result result;

    if (argument == NULL || out_identity == NULL)
    {
        return invalid_argument();
    }

    *out_identity = {};
    out_identity->kind = argument->kind == ZACLR_GENERIC_ARGUMENT_KIND_CONCRETE_TYPE ? ZACLR_TYPE_IDENTITY_KIND_CONCRETE
        : argument->kind == ZACLR_GENERIC_ARGUMENT_KIND_PRIMITIVE ? ZACLR_TYPE_IDENTITY_KIND_PRIMITIVE
        : argument->kind == ZACLR_GENERIC_ARGUMENT_KIND_TYPE_VAR ? ZACLR_TYPE_IDENTITY_KIND_TYPE_VAR
        : argument->kind == ZACLR_GENERIC_ARGUMENT_KIND_METHOD_VAR ? ZACLR_TYPE_IDENTITY_KIND_METHOD_VAR
        : argument->kind == ZACLR_GENERIC_ARGUMENT_KIND_GENERIC_INST ? ZACLR_TYPE_IDENTITY_KIND_GENERIC_INST
        : ZACLR_TYPE_IDENTITY_KIND_NONE;
    out_identity->element_type = argument->element_type;
    out_identity->token = argument->token;
    out_identity->assembly = argument->assembly;
    out_identity->generic_param_index = argument->generic_param_index;
    out_identity->generic_arg_count = argument->instantiation_arg_count;

    if (argument->instantiation_arg_count == 0u || argument->instantiation_args == NULL)
    {
        return zaclr_result_ok();
    }

    result = allocate_identities(argument->instantiation_arg_count, &out_identity->generic_args);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    for (index = 0u; index < argument->instantiation_arg_count; ++index)
    {
        result = zaclr_type_identity_from_generic_argument(&argument->instantiation_args[index],
                                                           &out_identity->generic_args[index]);
        if (result.status != ZACLR_STATUS_OK)
        {
            zaclr_type_identity_reset(out_identity);
            return result;
        }
    }

    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_type_identity_from_signature_type(const struct zaclr_generic_context* generic_context,
                                                                        const struct zaclr_loaded_assembly* current_assembly,
                                                                        struct zaclr_runtime* runtime,
                                                                        const struct zaclr_signature_type* signature_type,
                                                                        struct zaclr_type_identity* out_identity)
{
    struct zaclr_generic_argument argument = {};
    struct zaclr_result result;

    if (signature_type == NULL || out_identity == NULL)
    {
        return invalid_argument();
    }

    result = zaclr_generic_context_resolve_signature_type(generic_context,
                                                          current_assembly,
                                                          runtime,
                                                          signature_type,
                                                          &argument);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_type_identity_from_generic_argument(&argument, out_identity);
    reset_generic_argument(&argument);
    return result;
}

extern "C" struct zaclr_result zaclr_type_identity_from_token(const struct zaclr_generic_context* generic_context,
                                                               const struct zaclr_loaded_assembly* current_assembly,
                                                               struct zaclr_runtime* runtime,
                                                               struct zaclr_token token,
                                                               struct zaclr_type_identity* out_identity)
{
    const struct zaclr_loaded_assembly* resolved_assembly = NULL;
    const struct zaclr_type_desc* resolved_type = NULL;
    struct zaclr_typespec_desc typespec = {};
    struct zaclr_result result;

    if (current_assembly == NULL || out_identity == NULL)
    {
        return invalid_argument();
    }

    *out_identity = {};

    if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF)
        || zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEREF))
    {
        result = zaclr_type_system_resolve_type_desc(current_assembly,
                                                     runtime,
                                                     token,
                                                     &resolved_assembly,
                                                     &resolved_type);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (resolved_type == NULL)
        {
            return not_found();
        }

        out_identity->kind = ZACLR_TYPE_IDENTITY_KIND_CONCRETE;
        out_identity->element_type = ZACLR_ELEMENT_TYPE_CLASS;
        out_identity->token = resolved_type->token;
        out_identity->assembly = resolved_assembly;
        return zaclr_result_ok();
    }

    if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPESPEC))
    {
        result = zaclr_type_system_parse_typespec(current_assembly, runtime, token, &typespec);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        out_identity->kind = ZACLR_TYPE_IDENTITY_KIND_GENERIC_INST;
        out_identity->element_type = typespec.element_type;
        out_identity->token = typespec.generic_type_token;
        out_identity->assembly = typespec.generic_type_assembly != NULL ? typespec.generic_type_assembly : current_assembly;

        if (typespec.generic_context.type_arg_count != 0u && typespec.generic_context.type_args != NULL)
        {
            result = allocate_identities(typespec.generic_context.type_arg_count, &out_identity->generic_args);
            if (result.status != ZACLR_STATUS_OK)
            {
                zaclr_type_system_reset_typespec_desc(&typespec);
                return result;
            }

            out_identity->generic_arg_count = typespec.generic_context.type_arg_count;
            for (uint32_t index = 0u; index < typespec.generic_context.type_arg_count; ++index)
            {
                result = zaclr_type_identity_from_generic_argument(&typespec.generic_context.type_args[index],
                                                                   &out_identity->generic_args[index]);
                if (result.status != ZACLR_STATUS_OK)
                {
                    zaclr_type_system_reset_typespec_desc(&typespec);
                    zaclr_type_identity_reset(out_identity);
                    return result;
                }
            }
        }

        zaclr_type_system_reset_typespec_desc(&typespec);
        return zaclr_result_ok();
    }

    (void)generic_context;
    return not_found();
}

extern "C" struct zaclr_result zaclr_type_identity_materialize_runtime_type_handle(struct zaclr_runtime* runtime,
                                                                                     const struct zaclr_type_identity* identity,
                                                                                     zaclr_object_handle* out_handle)
{
    const struct zaclr_loaded_assembly* assembly;
    uint32_t type_row;
    zaclr_object_handle handle;
    struct zaclr_runtime_type_desc* runtime_type;
    struct zaclr_result result;

    if (runtime == NULL || identity == NULL || out_handle == NULL)
    {
        return invalid_argument();
    }

    *out_handle = 0u;
    if (identity->kind != ZACLR_TYPE_IDENTITY_KIND_CONCRETE
        && identity->kind != ZACLR_TYPE_IDENTITY_KIND_GENERIC_INST)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_METADATA);
    }

    assembly = identity->assembly;
    if (assembly == NULL || !zaclr_token_matches_table(&identity->token, ZACLR_TOKEN_TABLE_TYPEDEF))
    {
        return not_found();
    }

    type_row = zaclr_token_row(&identity->token);
    if (type_row == 0u || type_row > assembly->runtime_type_cache_count || assembly->runtime_type_cache == NULL)
    {
        return not_found();
    }

    handle = assembly->runtime_type_cache[type_row - 1u];
    if (handle != 0u)
    {
        *out_handle = handle;
        return zaclr_result_ok();
    }

    result = zaclr_runtime_type_allocate(&runtime->heap,
                                         assembly,
                                         identity->token,
                                         &runtime_type);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    handle = zaclr_heap_get_object_handle(&runtime->heap, &runtime_type->object);
    ((struct zaclr_loaded_assembly*)assembly)->runtime_type_cache[type_row - 1u] = handle;
    *out_handle = handle;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_type_identity_from_runtime_type_handle(struct zaclr_runtime* runtime,
                                                                              zaclr_object_handle handle,
                                                                              struct zaclr_type_identity* out_identity)
{
    const struct zaclr_runtime_type_desc* runtime_type;

    if (runtime == NULL || out_identity == NULL)
    {
        return invalid_argument();
    }

    *out_identity = {};
    if (handle == 0u)
    {
        return not_found();
    }

    runtime_type = zaclr_runtime_type_from_handle_const(&runtime->heap, handle);
    if (runtime_type == NULL)
    {
        return not_found();
    }

    out_identity->kind = ZACLR_TYPE_IDENTITY_KIND_CONCRETE;
    out_identity->element_type = ZACLR_ELEMENT_TYPE_CLASS;
    out_identity->token = runtime_type->type_token;
    out_identity->assembly = runtime_type->type_assembly;
    return zaclr_result_ok();
}
