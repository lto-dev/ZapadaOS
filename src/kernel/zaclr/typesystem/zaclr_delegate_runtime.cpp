#include <kernel/zaclr/typesystem/zaclr_delegate_runtime.h>

#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_method_handle.h>
#include <kernel/zaclr/typesystem/zaclr_method_table.h>
#include <kernel/zaclr/typesystem/zaclr_type_prepare.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

namespace
{
    static struct zaclr_result invalid_argument(void)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    static struct zaclr_result not_found(void)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    static struct zaclr_result resolve_named_instance_field(const struct zaclr_loaded_assembly* assembly,
                                                            const struct zaclr_type_desc* type_desc,
                                                            const char* field_name,
                                                            struct zaclr_token* out_token)
    {
        uint32_t field_index;
        struct zaclr_result result;

        if (assembly == NULL || type_desc == NULL || field_name == NULL || out_token == NULL)
        {
            return invalid_argument();
        }

        *out_token = zaclr_token_make(0u);

        if (zaclr_token_matches_table(&type_desc->extends, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            const struct zaclr_type_desc* base_type = zaclr_type_map_find_by_token(&assembly->type_map, type_desc->extends);
            if (base_type != NULL)
            {
                result = resolve_named_instance_field(assembly, base_type, field_name, out_token);
                if (result.status == ZACLR_STATUS_OK)
                {
                    return result;
                }
            }
        }

        for (field_index = 0u; field_index < type_desc->field_count; ++field_index)
        {
            uint32_t field_row = type_desc->field_list + field_index;
            struct zaclr_field_row row = {};
            struct zaclr_name_view resolved_name = {};
            result = zaclr_metadata_reader_get_field_row(&assembly->metadata, field_row, &row);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_metadata_reader_get_string(&assembly->metadata, row.name_index, &resolved_name);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (resolved_name.text != NULL && zaclr_text_equals(resolved_name.text, field_name))
            {
                *out_token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | field_row);
                return zaclr_result_ok();
            }
        }

        return not_found();
    }
}

extern "C" struct zaclr_result zaclr_delegate_runtime_resolve_field_tokens(struct zaclr_runtime* runtime,
                                                                             const struct zaclr_loaded_assembly* assembly,
                                                                             const struct zaclr_type_desc* delegate_type,
                                                                             struct zaclr_delegate_field_tokens* out_tokens)
{
    const struct zaclr_method_table* method_table;
    struct zaclr_result result;

    if (runtime == NULL || assembly == NULL || delegate_type == NULL || out_tokens == NULL)
    {
        return invalid_argument();
    }

    *out_tokens = {};
    result = zaclr_type_prepare(runtime,
                                (struct zaclr_loaded_assembly*)assembly,
                                delegate_type,
                                (struct zaclr_method_table**)&method_table);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = resolve_named_instance_field(assembly, delegate_type, "_target", &out_tokens->target_field);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = resolve_named_instance_field(assembly, delegate_type, "_methodBase", &out_tokens->method_base_field);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = resolve_named_instance_field(assembly, delegate_type, "_methodPtr", &out_tokens->method_ptr_field);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = resolve_named_instance_field(assembly, delegate_type, "_methodPtrAux", &out_tokens->method_ptr_aux_field);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)resolve_named_instance_field(assembly, delegate_type, "_invocationList", &out_tokens->invocation_list_field);
    (void)resolve_named_instance_field(assembly, delegate_type, "_invocationCount", &out_tokens->invocation_count_field);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_delegate_runtime_bind_singlecast(struct zaclr_runtime* runtime,
                                                                         struct zaclr_object_desc* delegate_object,
                                                                         const struct zaclr_loaded_assembly* assembly,
                                                                         const struct zaclr_type_desc* delegate_type,
                                                                         const struct zaclr_stack_value* target_value,
                                                                         const struct zaclr_method_handle* method_handle)
{
    struct zaclr_delegate_field_tokens tokens = {};
    struct zaclr_stack_value target_field = {};
    struct zaclr_stack_value method_ptr_field = {};
    struct zaclr_stack_value method_ptr_aux_field = {};
    struct zaclr_result result;

    if (runtime == NULL || delegate_object == NULL || assembly == NULL || delegate_type == NULL || method_handle == NULL)
    {
        return invalid_argument();
    }

    result = zaclr_delegate_runtime_resolve_field_tokens(runtime, assembly, delegate_type, &tokens);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (target_value != NULL)
    {
        target_field = *target_value;
    }
    result = zaclr_object_store_field(runtime, delegate_object, tokens.target_field, &target_field);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    method_ptr_field.kind = ZACLR_STACK_VALUE_I8;
    method_ptr_field.data.i8 = (int64_t)zaclr_method_handle_pack(method_handle);
    result = zaclr_object_store_field(runtime, delegate_object, tokens.method_ptr_field, &method_ptr_field);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    method_ptr_aux_field.kind = ZACLR_STACK_VALUE_I8;
    method_ptr_aux_field.data.i8 = 0;
    result = zaclr_object_store_field(runtime, delegate_object, tokens.method_ptr_aux_field, &method_ptr_aux_field);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_result_ok();
}
