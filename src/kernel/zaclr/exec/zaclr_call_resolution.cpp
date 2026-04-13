#include <kernel/zaclr/exec/zaclr_call_resolution.h>

#include <kernel/zaclr/loader/zaclr_binder.h>
#include <kernel/zaclr/typesystem/zaclr_member_resolution.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

namespace
{
    static bool text_equals(const char* left, const char* right)
    {
        return zaclr_text_equals(left, right);
    }
}

extern "C" struct zaclr_result zaclr_dispatch_resolve_type_desc(const struct zaclr_loaded_assembly* current_assembly,
                                                                struct zaclr_runtime* runtime,
                                                                struct zaclr_token token,
                                                                const struct zaclr_loaded_assembly** out_assembly,
                                                                const struct zaclr_type_desc** out_type)
{
    if (current_assembly == NULL || runtime == NULL || out_assembly == NULL || out_type == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    *out_assembly = NULL;
    *out_type = NULL;

    {
        struct zaclr_result result = zaclr_type_system_resolve_type_desc(current_assembly,
                                                                         runtime,
                                                                         token,
                                                                         out_assembly,
                                                                         out_type);
        if (result.status == ZACLR_STATUS_OK)
        {
            return zaclr_result_ok();
        }

        if (result.status == ZACLR_STATUS_NOT_IMPLEMENTED)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (result.status != ZACLR_STATUS_NOT_FOUND)
        {
            return result;
        }
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
}

extern "C" bool zaclr_dispatch_is_system_object_ctor(const struct zaclr_memberref_target* memberref)
{
    return memberref != NULL
        && text_equals(memberref->key.type_namespace, "System")
        && text_equals(memberref->key.type_name, "Object")
        && text_equals(memberref->key.method_name, ".ctor");
}
