#include <kernel/zaclr/typesystem/zaclr_delegate_runtime.h>

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/exec/zaclr_interop_dispatch.h>
#include <kernel/zaclr/heap/zaclr_array.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_method_handle.h>
#include <kernel/zaclr/typesystem/zaclr_method_table.h>
#include <kernel/zaclr/typesystem/zaclr_type_prepare.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

namespace
{
    static constexpr uint16_t k_field_flag_static = 0x0010u;

    static struct zaclr_result invalid_argument(void)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    static struct zaclr_result not_found(void)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    static struct zaclr_result resolve_named_instance_field_on_type(const struct zaclr_loaded_assembly* assembly,
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

            if ((row.flags & k_field_flag_static) == 0u
                && resolved_name.text != NULL
                && zaclr_text_equals(resolved_name.text, field_name))
            {
                *out_token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | field_row);
                return zaclr_result_ok();
            }
        }

        return not_found();
    }

    static struct zaclr_result resolve_named_instance_field_from_method_table(const struct zaclr_method_table* method_table,
                                                                               const char* field_name,
                                                                               struct zaclr_token* out_token)
    {
        const struct zaclr_method_table* current;

        if (method_table == NULL || field_name == NULL || out_token == NULL)
        {
            return invalid_argument();
        }

        *out_token = zaclr_token_make(0u);
        current = method_table;
        while (current != NULL)
        {
            if (current->assembly != NULL && current->type_desc != NULL)
            {
                struct zaclr_result result = resolve_named_instance_field_on_type(current->assembly,
                                                                                  current->type_desc,
                                                                                  field_name,
                                                                                  out_token);
                if (result.status == ZACLR_STATUS_OK)
                {
                    return result;
                }

                if (result.status != ZACLR_STATUS_NOT_FOUND)
                {
                    return result;
                }
            }

            current = current->parent;
        }

        return not_found();
    }

    static struct zaclr_result stack_value_to_object_reference(const struct zaclr_stack_value* value,
                                                               struct zaclr_object_desc** out_object)
    {
        if (value == NULL || out_object == NULL)
        {
            return invalid_argument();
        }

        if (value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_object = value->data.object_reference;
        return zaclr_result_ok();
    }

    static struct zaclr_result stack_value_to_i32(const struct zaclr_stack_value* value,
                                                  int32_t* out_value)
    {
        if (value == NULL || out_value == NULL)
        {
            return invalid_argument();
        }

        if (value->kind == ZACLR_STACK_VALUE_I4)
        {
            *out_value = value->data.i4;
            return zaclr_result_ok();
        }

        if (value->kind == ZACLR_STACK_VALUE_I8)
        {
            *out_value = (int32_t)value->data.i8;
            return zaclr_result_ok();
        }

        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    static bool object_value_equals(struct zaclr_runtime* runtime,
                                    const struct zaclr_stack_value* left,
                                    const struct zaclr_stack_value* right)
    {
        zaclr_object_handle left_handle;
        zaclr_object_handle right_handle;

        if (runtime == NULL || left == NULL || right == NULL)
        {
            return false;
        }

        if (left->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE || right->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        {
            return false;
        }

        left_handle = zaclr_heap_get_object_handle(&runtime->heap, left->data.object_reference);
        right_handle = zaclr_heap_get_object_handle(&runtime->heap, right->data.object_reference);
        return left_handle == right_handle;
    }

    static struct zaclr_result load_optional_delegate_field(struct zaclr_runtime* runtime,
                                                            const struct zaclr_object_desc* delegate_object,
                                                            struct zaclr_token token,
                                                            struct zaclr_stack_value* out_value)
    {
        if (runtime == NULL || delegate_object == NULL || out_value == NULL)
        {
            return invalid_argument();
        }

        *out_value = {};
        if (zaclr_token_is_nil(&token) || token.raw == 0u)
        {
            return zaclr_result_ok();
        }

        return zaclr_object_load_field(runtime, delegate_object, token, out_value);
    }

    static struct zaclr_result store_optional_delegate_field(struct zaclr_runtime* runtime,
                                                             struct zaclr_object_desc* delegate_object,
                                                             struct zaclr_token token,
                                                             const struct zaclr_stack_value* value)
    {
        if (runtime == NULL || delegate_object == NULL || value == NULL)
        {
            return invalid_argument();
        }

        if (zaclr_token_is_nil(&token) || token.raw == 0u)
        {
            return zaclr_result_ok();
        }

        return zaclr_object_store_field(runtime, delegate_object, token, value);
    }

    static struct zaclr_result get_delegate_invocation_count(struct zaclr_runtime* runtime,
                                                             const struct zaclr_object_desc* delegate_object,
                                                             const struct zaclr_delegate_field_tokens* tokens,
                                                             uint32_t* out_count)
    {
        struct zaclr_stack_value invocation_list_value = {};
        struct zaclr_stack_value invocation_count_value = {};
        struct zaclr_object_desc* invocation_list_object = NULL;
        struct zaclr_result result;

        if (runtime == NULL || delegate_object == NULL || tokens == NULL || out_count == NULL)
        {
            return invalid_argument();
        }

        *out_count = 1u;
        if (zaclr_token_is_nil(&tokens->invocation_list_field) || tokens->invocation_list_field.raw == 0u)
        {
            return zaclr_result_ok();
        }

        result = load_optional_delegate_field(runtime, delegate_object, tokens->invocation_list_field, &invocation_list_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (invocation_list_value.kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE || invocation_list_value.data.object_reference == NULL)
        {
            return zaclr_result_ok();
        }

        invocation_list_object = invocation_list_value.data.object_reference;
        if (zaclr_object_family(invocation_list_object) != ZACLR_OBJECT_FAMILY_ARRAY)
        {
            return zaclr_result_ok();
        }

        if (!zaclr_token_is_nil(&tokens->invocation_count_field)
            && tokens->invocation_count_field.raw != 0u
            && load_optional_delegate_field(runtime, delegate_object, tokens->invocation_count_field, &invocation_count_value).status == ZACLR_STATUS_OK)
        {
            int32_t count = 0;
            if (stack_value_to_i32(&invocation_count_value, &count).status == ZACLR_STATUS_OK && count > 0)
            {
                *out_count = (uint32_t)count;
                return zaclr_result_ok();
            }
        }

        *out_count = zaclr_array_length((struct zaclr_array_desc*)invocation_list_object);
        return zaclr_result_ok();
    }

    static struct zaclr_result flatten_delegate(struct zaclr_runtime* runtime,
                                                struct zaclr_object_desc* delegate_object,
                                                struct zaclr_object_desc** entries,
                                                uint32_t capacity,
                                                uint32_t* io_count)
    {
        const struct zaclr_method_table* method_table;
        const struct zaclr_loaded_assembly* assembly;
        const struct zaclr_type_desc* type;
        struct zaclr_delegate_field_tokens tokens = {};
        struct zaclr_stack_value invocation_list_value = {};
        struct zaclr_object_desc* invocation_list_object = NULL;
        struct zaclr_array_desc* invocation_list;
        uint32_t count = 1u;
        struct zaclr_result result;

        if (runtime == NULL || delegate_object == NULL || entries == NULL || io_count == NULL)
        {
            return invalid_argument();
        }

        method_table = delegate_object->header.method_table;
        assembly = method_table != NULL && method_table->assembly != NULL ? method_table->assembly : zaclr_object_owning_assembly(delegate_object);
        type = method_table != NULL ? method_table->type_desc : NULL;
        if (assembly == NULL || type == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
        }

        result = zaclr_delegate_runtime_resolve_field_tokens(runtime, assembly, type, &tokens);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = load_optional_delegate_field(runtime, delegate_object, tokens.invocation_list_field, &invocation_list_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (invocation_list_value.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE
            && invocation_list_value.data.object_reference != NULL
            && zaclr_object_family(invocation_list_value.data.object_reference) == ZACLR_OBJECT_FAMILY_ARRAY)
        {
            invocation_list_object = invocation_list_value.data.object_reference;
            invocation_list = (struct zaclr_array_desc*)invocation_list_object;
            result = get_delegate_invocation_count(runtime, delegate_object, &tokens, &count);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (count > zaclr_array_length(invocation_list))
            {
                count = zaclr_array_length(invocation_list);
            }

            for (uint32_t index = 0u; index < count; ++index)
            {
                zaclr_object_handle handle;
                struct zaclr_object_desc* entry;
                uint8_t* data = (uint8_t*)zaclr_array_data(invocation_list);
                if (data == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_HEAP);
                }

                kernel_memcpy(&handle, data + ((size_t)index * zaclr_array_element_size(invocation_list)), sizeof(handle));
                entry = zaclr_heap_get_object(&runtime->heap, handle);
                if (entry != NULL)
                {
                    if (*io_count >= capacity)
                    {
                        return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
                    }

                    entries[*io_count] = entry;
                    *io_count += 1u;
                }
            }

            return zaclr_result_ok();
        }

        if (*io_count >= capacity)
        {
            return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
        }

        entries[*io_count] = delegate_object;
        *io_count += 1u;
        return zaclr_result_ok();
    }

    static struct zaclr_result copy_singlecast_payload(struct zaclr_runtime* runtime,
                                                       struct zaclr_object_desc* destination,
                                                       struct zaclr_object_desc* source,
                                                       const struct zaclr_delegate_field_tokens* tokens)
    {
        struct zaclr_stack_value field_value = {};
        struct zaclr_result result;

        if (runtime == NULL || destination == NULL || source == NULL || tokens == NULL)
        {
            return invalid_argument();
        }

        result = zaclr_object_load_field(runtime, source, tokens->target_field, &field_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_object_store_field(runtime, destination, tokens->target_field, &field_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_object_load_field(runtime, source, tokens->method_ptr_field, &field_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_object_store_field(runtime, destination, tokens->method_ptr_field, &field_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = load_optional_delegate_field(runtime, source, tokens->method_ptr_aux_field, &field_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return store_optional_delegate_field(runtime, destination, tokens->method_ptr_aux_field, &field_value);
    }

    static bool delegate_stack_values_equal(struct zaclr_runtime* runtime,
                                            const struct zaclr_stack_value* left,
                                            const struct zaclr_stack_value* right)
    {
        if (runtime == NULL || left == NULL || right == NULL)
        {
            return false;
        }

        if (left->kind != right->kind)
        {
            return false;
        }

        switch (left->kind)
        {
            case ZACLR_STACK_VALUE_EMPTY:
                return true;

            case ZACLR_STACK_VALUE_I4:
                return left->data.i4 == right->data.i4;

            case ZACLR_STACK_VALUE_I8:
                return left->data.i8 == right->data.i8;

            case ZACLR_STACK_VALUE_OBJECT_REFERENCE:
                return zaclr_heap_get_object_handle(&runtime->heap, left->data.object_reference)
                    == zaclr_heap_get_object_handle(&runtime->heap, right->data.object_reference);

            default:
                return false;
        }
    }

    static struct zaclr_result delegates_are_equal(struct zaclr_runtime* runtime,
                                                   struct zaclr_object_desc* left,
                                                   struct zaclr_object_desc* right,
                                                   const struct zaclr_delegate_field_tokens* tokens,
                                                   bool* out_equal)
    {
        struct zaclr_stack_value left_target = {};
        struct zaclr_stack_value right_target = {};
        struct zaclr_stack_value left_method_ptr = {};
        struct zaclr_stack_value right_method_ptr = {};
        struct zaclr_stack_value left_method_ptr_aux = {};
        struct zaclr_stack_value right_method_ptr_aux = {};
        struct zaclr_result result;

        if (runtime == NULL || left == NULL || right == NULL || tokens == NULL || out_equal == NULL)
        {
            return invalid_argument();
        }

        *out_equal = false;
        result = zaclr_object_load_field(runtime, left, tokens->target_field, &left_target);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_object_load_field(runtime, right, tokens->target_field, &right_target);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_object_load_field(runtime, left, tokens->method_ptr_field, &left_method_ptr);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_object_load_field(runtime, right, tokens->method_ptr_field, &right_method_ptr);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = load_optional_delegate_field(runtime, left, tokens->method_ptr_aux_field, &left_method_ptr_aux);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = load_optional_delegate_field(runtime, right, tokens->method_ptr_aux_field, &right_method_ptr_aux);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_equal = delegate_stack_values_equal(runtime, &left_target, &right_target)
            && delegate_stack_values_equal(runtime, &left_method_ptr, &right_method_ptr)
            && delegate_stack_values_equal(runtime, &left_method_ptr_aux, &right_method_ptr_aux);
        return zaclr_result_ok();
    }

    static struct zaclr_result build_multicast_delegate(struct zaclr_runtime* runtime,
                                                        const struct zaclr_loaded_assembly* assembly,
                                                        const struct zaclr_type_desc* delegate_type,
                                                        const struct zaclr_delegate_field_tokens* tokens,
                                                        struct zaclr_object_desc** entries,
                                                        uint32_t entry_count,
                                                        struct zaclr_stack_value* out_value)
    {
        struct zaclr_object_desc* combined_object = NULL;
        struct zaclr_array_desc* invocation_list = NULL;
        zaclr_object_handle combined_handle;
        uint8_t* invocation_data;
        struct zaclr_stack_value invocation_list_value = {};
        struct zaclr_stack_value invocation_count_value = {};
        struct zaclr_stack_value target_value = {};
        struct zaclr_result result;

        if (runtime == NULL || assembly == NULL || delegate_type == NULL || tokens == NULL || entries == NULL || out_value == NULL)
        {
            return invalid_argument();
        }

        *out_value = {};
        if (entry_count == 0u)
        {
            out_value->kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
            out_value->data.object_reference = NULL;
            return zaclr_result_ok();
        }

        if (entry_count == 1u)
        {
            out_value->kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
            out_value->data.object_reference = entries[0];
            return zaclr_result_ok();
        }

        result = zaclr_allocate_reference_type_instance(runtime, assembly, delegate_type->token, &combined_object);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = copy_singlecast_payload(runtime, combined_object, entries[entry_count - 1u], tokens);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_array_allocate(&runtime->heap,
                                      0u,
                                      delegate_type->token,
                                      sizeof(zaclr_object_handle),
                                      entry_count,
                                      &invocation_list);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        invocation_data = (uint8_t*)zaclr_array_data(invocation_list);
        if (invocation_data == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_HEAP);
        }

        for (uint32_t index = 0u; index < entry_count; ++index)
        {
            zaclr_object_handle entry_handle = zaclr_heap_get_object_handle(&runtime->heap, entries[index]);
            kernel_memcpy(invocation_data + ((size_t)index * sizeof(entry_handle)), &entry_handle, sizeof(entry_handle));
        }

        invocation_list_value.kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        invocation_list_value.data.object_reference = &invocation_list->object;
        result = store_optional_delegate_field(runtime, combined_object, tokens->invocation_list_field, &invocation_list_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        invocation_count_value.kind = ZACLR_STACK_VALUE_I4;
        invocation_count_value.data.i4 = (int32_t)entry_count;
        result = store_optional_delegate_field(runtime, combined_object, tokens->invocation_count_field, &invocation_count_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        combined_handle = zaclr_heap_get_object_handle(&runtime->heap, combined_object);
        target_value.kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        target_value.data.object_reference = combined_object;
        result = zaclr_object_store_field(runtime, combined_object, tokens->target_field, &target_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        out_value->kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        out_value->data.object_reference = zaclr_heap_get_object(&runtime->heap, combined_handle);
        return object_value_equals(runtime, out_value, &target_value) || out_value->data.object_reference != NULL
            ? zaclr_result_ok()
            : zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_HEAP);
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

    result = resolve_named_instance_field_from_method_table(method_table, "_target", &out_tokens->target_field);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = resolve_named_instance_field_from_method_table(method_table, "_methodBase", &out_tokens->method_base_field);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = resolve_named_instance_field_from_method_table(method_table, "_methodPtr", &out_tokens->method_ptr_field);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = resolve_named_instance_field_from_method_table(method_table, "_methodPtrAux", &out_tokens->method_ptr_aux_field);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)resolve_named_instance_field_from_method_table(method_table, "_invocationList", &out_tokens->invocation_list_field);
    (void)resolve_named_instance_field_from_method_table(method_table, "_invocationCount", &out_tokens->invocation_count_field);
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
    method_ptr_aux_field.data.i8 = (int64_t)method_handle->locator.assembly_id;
    result = zaclr_object_store_field(runtime, delegate_object, tokens.method_ptr_aux_field, &method_ptr_aux_field);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_delegate_runtime_combine(struct zaclr_runtime* runtime,
                                                                  const struct zaclr_stack_value* left,
                                                                  const struct zaclr_stack_value* right,
                                                                  struct zaclr_stack_value* out_value)
{
    static constexpr uint32_t k_max_flattened_invocation_count = 32u;
    struct zaclr_object_desc* left_object = NULL;
    struct zaclr_object_desc* right_object = NULL;
    struct zaclr_object_desc* entries[k_max_flattened_invocation_count] = {};
    uint32_t entry_count = 0u;
    const struct zaclr_method_table* method_table;
    const struct zaclr_loaded_assembly* assembly;
    const struct zaclr_type_desc* delegate_type;
    struct zaclr_delegate_field_tokens tokens = {};
    struct zaclr_result result;

    if (runtime == NULL || left == NULL || right == NULL || out_value == NULL)
    {
        return invalid_argument();
    }

    *out_value = {};
    result = stack_value_to_object_reference(left, &left_object);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = stack_value_to_object_reference(right, &right_object);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (left_object == NULL)
    {
        return zaclr_stack_value_assign(out_value, right);
    }

    if (right_object == NULL)
    {
        return zaclr_stack_value_assign(out_value, left);
    }

    method_table = left_object->header.method_table;
    assembly = method_table != NULL && method_table->assembly != NULL ? method_table->assembly : zaclr_object_owning_assembly(left_object);
    delegate_type = method_table != NULL ? method_table->type_desc : NULL;
    if (assembly == NULL || delegate_type == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    result = flatten_delegate(runtime, left_object, entries, k_max_flattened_invocation_count, &entry_count);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = flatten_delegate(runtime, right_object, entries, k_max_flattened_invocation_count, &entry_count);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (entry_count == 0u)
    {
        return zaclr_stack_value_assign(out_value, left);
    }

    result = zaclr_delegate_runtime_resolve_field_tokens(runtime, assembly, delegate_type, &tokens);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return build_multicast_delegate(runtime, assembly, delegate_type, &tokens, entries, entry_count, out_value);
}

extern "C" struct zaclr_result zaclr_delegate_runtime_remove(struct zaclr_runtime* runtime,
                                                                 const struct zaclr_stack_value* source,
                                                                 const struct zaclr_stack_value* value,
                                                                 struct zaclr_stack_value* out_value)
{
    static constexpr uint32_t k_max_flattened_invocation_count = 32u;
    struct zaclr_object_desc* source_object = NULL;
    struct zaclr_object_desc* value_object = NULL;
    struct zaclr_object_desc* source_entries[k_max_flattened_invocation_count] = {};
    struct zaclr_object_desc* value_entries[k_max_flattened_invocation_count] = {};
    uint32_t source_count = 0u;
    uint32_t value_count = 0u;
    uint32_t remove_start = 0u;
    bool found_match = false;
    const struct zaclr_method_table* method_table;
    const struct zaclr_loaded_assembly* assembly;
    const struct zaclr_type_desc* delegate_type;
    struct zaclr_delegate_field_tokens tokens = {};
    struct zaclr_result result;

    if (runtime == NULL || source == NULL || value == NULL || out_value == NULL)
    {
        return invalid_argument();
    }

    *out_value = {};
    result = stack_value_to_object_reference(source, &source_object);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = stack_value_to_object_reference(value, &value_object);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (source_object == NULL)
    {
        out_value->kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        out_value->data.object_reference = NULL;
        return zaclr_result_ok();
    }

    if (value_object == NULL)
    {
        return zaclr_stack_value_assign(out_value, source);
    }

    method_table = source_object->header.method_table;
    assembly = method_table != NULL && method_table->assembly != NULL ? method_table->assembly : zaclr_object_owning_assembly(source_object);
    delegate_type = method_table != NULL ? method_table->type_desc : NULL;
    if (assembly == NULL || delegate_type == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    result = zaclr_delegate_runtime_resolve_field_tokens(runtime, assembly, delegate_type, &tokens);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = flatten_delegate(runtime, source_object, source_entries, k_max_flattened_invocation_count, &source_count);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = flatten_delegate(runtime, value_object, value_entries, k_max_flattened_invocation_count, &value_count);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (value_count == 0u || value_count > source_count)
    {
        return zaclr_stack_value_assign(out_value, source);
    }

    for (uint32_t candidate_start = source_count - value_count + 1u; candidate_start > 0u; --candidate_start)
    {
        uint32_t start = candidate_start - 1u;
        bool sequence_equal = true;

        for (uint32_t index = 0u; index < value_count; ++index)
        {
            bool entry_equal = false;
            result = delegates_are_equal(runtime, source_entries[start + index], value_entries[index], &tokens, &entry_equal);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (!entry_equal)
            {
                sequence_equal = false;
                break;
            }
        }

        if (sequence_equal)
        {
            found_match = true;
            remove_start = start;
            break;
        }
    }

    if (!found_match)
    {
        return zaclr_stack_value_assign(out_value, source);
    }

    for (uint32_t index = remove_start + value_count; index < source_count; ++index)
    {
        source_entries[index - value_count] = source_entries[index];
    }

    source_count -= value_count;
    return build_multicast_delegate(runtime, assembly, delegate_type, &tokens, source_entries, source_count, out_value);
}

extern "C" struct zaclr_result zaclr_delegate_runtime_equals(struct zaclr_runtime* runtime,
                                                                 const struct zaclr_stack_value* left,
                                                                 const struct zaclr_stack_value* right,
                                                                 uint8_t* out_equal)
{
    static constexpr uint32_t k_max_flattened_invocation_count = 32u;
    struct zaclr_object_desc* left_object = NULL;
    struct zaclr_object_desc* right_object = NULL;
    struct zaclr_object_desc* left_entries[k_max_flattened_invocation_count] = {};
    struct zaclr_object_desc* right_entries[k_max_flattened_invocation_count] = {};
    uint32_t left_count = 0u;
    uint32_t right_count = 0u;
    const struct zaclr_method_table* method_table;
    const struct zaclr_loaded_assembly* assembly;
    const struct zaclr_type_desc* delegate_type;
    struct zaclr_delegate_field_tokens tokens = {};
    struct zaclr_result result;

    if (runtime == NULL || left == NULL || right == NULL || out_equal == NULL)
    {
        return invalid_argument();
    }

    *out_equal = 0u;
    result = stack_value_to_object_reference(left, &left_object);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = stack_value_to_object_reference(right, &right_object);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (left_object == NULL || right_object == NULL)
    {
        *out_equal = left_object == right_object ? 1u : 0u;
        return zaclr_result_ok();
    }

    if (left_object->header.method_table != right_object->header.method_table)
    {
        return zaclr_result_ok();
    }

    method_table = left_object->header.method_table;
    assembly = method_table != NULL && method_table->assembly != NULL ? method_table->assembly : zaclr_object_owning_assembly(left_object);
    delegate_type = method_table != NULL ? method_table->type_desc : NULL;
    if (assembly == NULL || delegate_type == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
    }

    result = zaclr_delegate_runtime_resolve_field_tokens(runtime, assembly, delegate_type, &tokens);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = flatten_delegate(runtime, left_object, left_entries, k_max_flattened_invocation_count, &left_count);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = flatten_delegate(runtime, right_object, right_entries, k_max_flattened_invocation_count, &right_count);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (left_count != right_count)
    {
        return zaclr_result_ok();
    }

    for (uint32_t index = 0u; index < left_count; ++index)
    {
        bool entry_equal = false;
        result = delegates_are_equal(runtime, left_entries[index], right_entries[index], &tokens, &entry_equal);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (!entry_equal)
        {
            return zaclr_result_ok();
        }
    }

    *out_equal = 1u;
    return zaclr_result_ok();
}
