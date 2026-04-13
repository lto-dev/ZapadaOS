#include <kernel/zaclr/typesystem/zaclr_call_target.h>

#include <kernel/zaclr/interop/zaclr_native_assembly.h>
#include <kernel/zaclr/loader/zaclr_binder.h>
#include <kernel/zaclr/typesystem/zaclr_member_resolution.h>

namespace
{
    static struct zaclr_result invalid_argument(void)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }
}

extern "C" void zaclr_call_target_reset(struct zaclr_call_target* target)
{
    if (target == NULL)
    {
        return;
    }

    zaclr_type_system_reset_typespec_desc(&target->owning_typespec);
    zaclr_generic_context_reset(&target->method_generic_context);
    *target = {};
}

extern "C" struct zaclr_result zaclr_call_target_clone(struct zaclr_call_target* destination,
                                                        const struct zaclr_call_target* source)
{
    struct zaclr_result result;

    if (destination == NULL)
    {
        return invalid_argument();
    }

    zaclr_call_target_reset(destination);
    if (source == NULL)
    {
        return zaclr_result_ok();
    }

    destination->assembly = source->assembly;
    destination->owning_type = source->owning_type;
    destination->method = source->method;
    destination->has_owning_typespec = source->has_owning_typespec;
    destination->has_method_instantiation = source->has_method_instantiation;

    if (source->has_owning_typespec != 0u)
    {
        destination->owning_typespec.element_type = source->owning_typespec.element_type;
        destination->owning_typespec.is_generic_instantiation = source->owning_typespec.is_generic_instantiation;
        destination->owning_typespec.generic_type_token = source->owning_typespec.generic_type_token;
        destination->owning_typespec.generic_type_assembly = source->owning_typespec.generic_type_assembly;
        result = zaclr_generic_context_clone(&destination->owning_typespec.generic_context,
                                             &source->owning_typespec.generic_context);
        if (result.status != ZACLR_STATUS_OK)
        {
            zaclr_call_target_reset(destination);
            return result;
        }
    }

    if (source->has_method_instantiation != 0u)
    {
        result = zaclr_generic_context_clone(&destination->method_generic_context,
                                             &source->method_generic_context);
        if (result.status != ZACLR_STATUS_OK)
        {
            zaclr_call_target_reset(destination);
            return result;
        }
    }

    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_call_target_resolve_memberref(const struct zaclr_runtime* runtime,
                                                                    const struct zaclr_loaded_assembly* source_assembly,
                                                                    const struct zaclr_memberref_target* memberref,
                                                                    const struct zaclr_slice* methodspec_instantiation,
                                                                    struct zaclr_call_target* out_target)
{
    struct zaclr_result result;

    if (runtime == NULL || source_assembly == NULL || memberref == NULL || out_target == NULL)
    {
        return invalid_argument();
    }

    zaclr_call_target_reset(out_target);
    result = zaclr_member_resolution_resolve_method(runtime,
                                                    source_assembly,
                                                    memberref,
                                                    &out_target->assembly,
                                                    &out_target->owning_type,
                                                    &out_target->method);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if ((memberref->class_token & 0x7u) == 4u)
    {
        struct zaclr_token typespec_token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPESPEC << 24) | (memberref->class_token >> 3u));
        result = zaclr_type_system_parse_typespec(source_assembly,
                                                  (struct zaclr_runtime*)runtime,
                                                  typespec_token,
                                                  &out_target->owning_typespec);
        if (result.status != ZACLR_STATUS_OK)
        {
            zaclr_call_target_reset(out_target);
            return result;
        }

        out_target->has_owning_typespec = 1u;
    }

    if (methodspec_instantiation != NULL && methodspec_instantiation->data != NULL && methodspec_instantiation->size != 0u)
    {
        result = zaclr_generic_context_set_method_instantiation(&out_target->method_generic_context,
                                                                source_assembly,
                                                                (struct zaclr_runtime*)runtime,
                                                                methodspec_instantiation);
        if (result.status != ZACLR_STATUS_OK)
        {
            zaclr_call_target_reset(out_target);
            return result;
        }

        out_target->has_method_instantiation = 1u;
    }

    return zaclr_result_ok();
}
