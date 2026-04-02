#include <kernel/zaclr/heap/zaclr_object.h>

#include <kernel/support/kernel_memory.h>

#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/loader/zaclr_assembly_registry.h>
#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>
#include <kernel/zaclr/metadata/zaclr_token.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

namespace
{
    static size_t zaclr_reference_object_allocation_size(uint32_t field_capacity)
    {
        return sizeof(struct zaclr_reference_object_desc)
             + (sizeof(uint32_t) * (size_t)field_capacity)
             + (sizeof(struct zaclr_stack_value) * (size_t)field_capacity);
    }

    static bool zaclr_try_get_field_name(const struct zaclr_loaded_assembly* assembly,
                                         struct zaclr_token token,
                                         const char** out_name)
    {
        struct zaclr_field_row field_row;
        struct zaclr_name_view field_name;

        if (assembly == NULL || out_name == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
        {
            return false;
        }

        if (zaclr_metadata_reader_get_field_row(&assembly->metadata, zaclr_token_row(&token), &field_row).status != ZACLR_STATUS_OK)
        {
            return false;
        }

        if (zaclr_metadata_reader_get_string(&assembly->metadata, field_row.name_index, &field_name).status != ZACLR_STATUS_OK)
        {
            return false;
        }

        *out_name = field_name.text;
        return true;
    }
}

extern "C" uint32_t zaclr_object_flags(const struct zaclr_object_desc* object)
{
    return object != NULL ? object->flags : 0u;
}

extern "C" uint32_t zaclr_object_size_bytes(const struct zaclr_object_desc* object)
{
    return object != NULL ? object->size_bytes : 0u;
}

extern "C" uint32_t zaclr_object_family(const struct zaclr_object_desc* object)
{
    return object != NULL ? object->family : (uint32_t)ZACLR_OBJECT_FAMILY_UNKNOWN;
}

extern "C" uint32_t zaclr_object_contains_references(const struct zaclr_object_desc* object)
{
    return object != NULL && (object->flags & ZACLR_OBJECT_FLAG_CONTAINS_REFERENCES) != 0u;
}

extern "C" uint32_t zaclr_object_is_marked(const struct zaclr_object_desc* object)
{
    return object != NULL && object->gc_mark != 0u;
}

extern "C" void zaclr_object_set_marked(struct zaclr_object_desc* object, uint32_t marked)
{
    if (object != NULL)
    {
        object->gc_mark = marked != 0u ? 1u : 0u;
    }
}

extern "C" struct zaclr_result zaclr_boxed_value_allocate(struct zaclr_heap* heap,
                                                            struct zaclr_token token,
                                                            const struct zaclr_stack_value* value,
                                                            zaclr_object_handle* out_handle)
{
    struct zaclr_boxed_value_desc* boxed_value;
    struct zaclr_result result;

    if (heap == NULL || value == NULL || out_handle == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    *out_handle = 0u;
    result = zaclr_heap_allocate_object(heap,
                                        sizeof(struct zaclr_boxed_value_desc),
                                        NULL,
                                        0u,
                                        ZACLR_OBJECT_FAMILY_INSTANCE,
                                        ZACLR_OBJECT_FLAG_BOXED_VALUE,
                                        (struct zaclr_object_desc**)&boxed_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    boxed_value->type_token_raw = token.raw;
    boxed_value->reserved = 0u;
    boxed_value->value = *value;
    *out_handle = boxed_value->object.handle;
    return zaclr_result_ok();
}

extern "C" struct zaclr_boxed_value_desc* zaclr_boxed_value_from_handle(struct zaclr_heap* heap,
                                                                          zaclr_object_handle handle)
{
    return (struct zaclr_boxed_value_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" const struct zaclr_boxed_value_desc* zaclr_boxed_value_from_handle_const(const struct zaclr_heap* heap,
                                                                                      zaclr_object_handle handle)
{
    return (const struct zaclr_boxed_value_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" struct zaclr_result zaclr_reference_object_allocate(struct zaclr_heap* heap,
                                                                  const struct zaclr_loaded_assembly* owning_assembly,
                                                                  zaclr_type_id type_id,
                                                                  struct zaclr_token type_token,
                                                                  uint32_t field_capacity,
                                                                  zaclr_object_handle* out_handle)
{
    struct zaclr_reference_object_desc* object;
    struct zaclr_result result;
    uint32_t* field_tokens;
    struct zaclr_stack_value* field_values;

    if (heap == NULL || out_handle == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    *out_handle = 0u;
    result = zaclr_heap_allocate_object(heap,
                                        zaclr_reference_object_allocation_size(field_capacity),
                                        owning_assembly,
                                        type_id,
                                        ZACLR_OBJECT_FAMILY_INSTANCE,
                                        ZACLR_OBJECT_FLAG_REFERENCE_TYPE | ZACLR_OBJECT_FLAG_CONTAINS_REFERENCES,
                                        (struct zaclr_object_desc**)&object);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    object->type_token_raw = type_token.raw;
    object->field_capacity = field_capacity;
    field_tokens = zaclr_reference_object_field_tokens(object);
    field_values = zaclr_reference_object_field_values(object);
    if (field_capacity != 0u)
    {
        kernel_memset(field_tokens, 0, sizeof(uint32_t) * (size_t)field_capacity);
        kernel_memset(field_values, 0, sizeof(struct zaclr_stack_value) * (size_t)field_capacity);
    }

    *out_handle = object->object.handle;
    return zaclr_result_ok();
}

extern "C" struct zaclr_reference_object_desc* zaclr_reference_object_from_handle(struct zaclr_heap* heap,
                                                                                    zaclr_object_handle handle)
{
    return (struct zaclr_reference_object_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" const struct zaclr_reference_object_desc* zaclr_reference_object_from_handle_const(const struct zaclr_heap* heap,
                                                                                                zaclr_object_handle handle)
{
    return (const struct zaclr_reference_object_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" struct zaclr_result zaclr_reference_object_store_field(struct zaclr_reference_object_desc* object,
                                                                     struct zaclr_token token,
                                                                     const struct zaclr_stack_value* value)
{
    uint32_t index;
    uint32_t* field_tokens;
    struct zaclr_stack_value* field_values;

    if (object == NULL || value == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    field_tokens = zaclr_reference_object_field_tokens(object);
    field_values = zaclr_reference_object_field_values(object);

    for (index = 0u; index < object->field_capacity; ++index)
    {
        if (field_tokens[index] == token.raw || field_tokens[index] == 0u)
        {
            field_tokens[index] = token.raw;
            field_values[index] = *value;
            return zaclr_result_ok();
        }
    }

    return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_HEAP);
}

extern "C" struct zaclr_result zaclr_reference_object_load_field(const struct zaclr_reference_object_desc* object,
                                                                    struct zaclr_token token,
                                                                    struct zaclr_stack_value* out_value)
{
    uint32_t index;
    const uint32_t* field_tokens;
    const struct zaclr_stack_value* field_values;

    if (object == NULL || out_value == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    field_tokens = zaclr_reference_object_field_tokens_const(object);
    field_values = zaclr_reference_object_field_values_const(object);

    for (index = 0u; index < object->field_capacity; ++index)
    {
        if (field_tokens[index] == token.raw)
        {
            *out_value = field_values[index];
            return zaclr_result_ok();
        }
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
}

extern "C" struct zaclr_stack_value* zaclr_reference_object_field_storage(struct zaclr_reference_object_desc* object,
                                                                            struct zaclr_token token)
{
    uint32_t* field_tokens;
    struct zaclr_stack_value* field_values;
    uint32_t index;

    if (object == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
    {
        return NULL;
    }

    field_tokens = zaclr_reference_object_field_tokens(object);
    field_values = zaclr_reference_object_field_values(object);
    for (index = 0u; index < object->field_capacity; ++index)
    {
        if (field_tokens[index] == token.raw)
        {
            return &field_values[index];
        }
    }

    return NULL;
}

extern "C" const struct zaclr_stack_value* zaclr_reference_object_field_storage_const(const struct zaclr_reference_object_desc* object,
                                                                                         struct zaclr_token token)
{
    return zaclr_reference_object_field_storage((struct zaclr_reference_object_desc*)object, token);
}

extern "C" struct zaclr_result zaclr_object_store_field(struct zaclr_runtime* runtime,
                                                          zaclr_object_handle handle,
                                                          struct zaclr_token token,
                                                          const struct zaclr_stack_value* value)
{
    struct zaclr_object_desc* object;

    if (runtime == NULL || value == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    object = zaclr_heap_get_object(&runtime->heap, handle);
    if (object == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    if ((zaclr_object_flags(object) & ZACLR_OBJECT_FLAG_BOXED_VALUE) != 0u)
    {
        struct zaclr_boxed_value_desc* boxed_value = (struct zaclr_boxed_value_desc*)object;
        if (zaclr_token_row(&token) == 1u || zaclr_token_row(&token) == 23u || zaclr_token_row(&token) == 25u)
        {
            boxed_value->value = *value;
            return zaclr_result_ok();
        }

        if (zaclr_token_row(&token) == 24u)
        {
            boxed_value->reserved = value->kind == ZACLR_STACK_VALUE_I4 ? (uint32_t)value->data.i4 : 0u;
            return zaclr_result_ok();
        }
    }

    if ((zaclr_object_flags(object) & ZACLR_OBJECT_FLAG_REFERENCE_TYPE) != 0u)
    {
        return zaclr_reference_object_store_field((struct zaclr_reference_object_desc*)object, token, value);
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_HEAP);
}

extern "C" struct zaclr_result zaclr_object_load_field(struct zaclr_runtime* runtime,
                                                         zaclr_object_handle handle,
                                                         struct zaclr_token token,
                                                         struct zaclr_stack_value* out_value)
{
    struct zaclr_object_desc* object;

    if (runtime == NULL || out_value == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    object = zaclr_heap_get_object(&runtime->heap, handle);
    if (object == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    if ((zaclr_object_flags(object) & ZACLR_OBJECT_FLAG_BOXED_VALUE) != 0u)
    {
        struct zaclr_boxed_value_desc* boxed_value = (struct zaclr_boxed_value_desc*)object;
        if (zaclr_token_row(&token) == 1u || zaclr_token_row(&token) == 23u || zaclr_token_row(&token) == 25u)
        {
            *out_value = boxed_value->value;
            return zaclr_result_ok();
        }

        if (zaclr_token_row(&token) == 24u)
        {
            out_value->kind = ZACLR_STACK_VALUE_I4;
            out_value->data.i4 = (int32_t)boxed_value->reserved;
            return zaclr_result_ok();
        }
    }

    if ((zaclr_object_flags(object) & ZACLR_OBJECT_FLAG_REFERENCE_TYPE) != 0u)
    {
        const char* field_name = NULL;
        const struct zaclr_loaded_assembly* field_assembly = object->owning_assembly != NULL
            ? object->owning_assembly
            : zaclr_assembly_registry_find_by_name(&runtime->assemblies, "System.Private.CoreLib");
        if ((zaclr_object_flags(object) & ZACLR_OBJECT_FLAG_STRING) != 0u
            && zaclr_try_get_field_name(field_assembly, token, &field_name))
        {
            const struct zaclr_string_desc* string_object = (const struct zaclr_string_desc*)object;
            if (zaclr_text_equals(field_name, "_stringLength") || zaclr_text_equals(field_name, "m_stringLength"))
            {
                out_value->kind = ZACLR_STACK_VALUE_I4;
                out_value->data.i4 = (int32_t)zaclr_string_length(string_object);
                return zaclr_result_ok();
            }

            if (zaclr_text_equals(field_name, "_firstChar") || zaclr_text_equals(field_name, "m_firstChar"))
            {
                out_value->kind = ZACLR_STACK_VALUE_I4;
                out_value->data.i4 = (int32_t)zaclr_string_char_at(string_object, 0u);
                return zaclr_result_ok();
            }
        }

        return zaclr_reference_object_load_field((const struct zaclr_reference_object_desc*)object, token, out_value);
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_HEAP);
}

extern "C" uint32_t* zaclr_reference_object_field_tokens(struct zaclr_reference_object_desc* object)
{
    return object != NULL ? (uint32_t*)(object + 1) : NULL;
}

extern "C" const uint32_t* zaclr_reference_object_field_tokens_const(const struct zaclr_reference_object_desc* object)
{
    return object != NULL ? (const uint32_t*)(object + 1) : NULL;
}

extern "C" struct zaclr_stack_value* zaclr_reference_object_field_values(struct zaclr_reference_object_desc* object)
{
    return object != NULL
        ? (struct zaclr_stack_value*)(zaclr_reference_object_field_tokens(object) + object->field_capacity)
        : NULL;
}

extern "C" const struct zaclr_stack_value* zaclr_reference_object_field_values_const(const struct zaclr_reference_object_desc* object)
{
    return object != NULL
        ? (const struct zaclr_stack_value*)(zaclr_reference_object_field_tokens_const(object) + object->field_capacity)
        : NULL;
}

extern "C" uint32_t zaclr_stack_value_contains_references(const struct zaclr_stack_value* value)
{
    if (value == NULL)
    {
        return 0u;
    }

    return value->kind == ZACLR_STACK_VALUE_OBJECT_HANDLE && value->data.object_handle != 0u;
}
