#include <kernel/zaclr/heap/zaclr_array.h>

namespace
{
    static uint32_t zaclr_array_object_flags(struct zaclr_token element_type)
    {
        const uint32_t table = zaclr_token_table(&element_type);
        const uint32_t contains_references =
            table == ZACLR_TOKEN_TABLE_TYPEREF ||
            table == ZACLR_TOKEN_TABLE_TYPEDEF ||
            table == ZACLR_TOKEN_TABLE_TYPESPEC;

        return ZACLR_OBJECT_FLAG_REFERENCE_TYPE |
               (contains_references != 0u ? (uint32_t)ZACLR_OBJECT_FLAG_CONTAINS_REFERENCES : 0u);
    }
}

extern "C" uint32_t zaclr_array_length(const struct zaclr_array_desc* value)
{
    return value != NULL ? value->length : 0u;
}

extern "C" struct zaclr_result zaclr_array_allocate(struct zaclr_heap* heap,
                                                        zaclr_type_id type_id,
                                                        struct zaclr_token element_type,
                                                        uint32_t element_size,
                                                        uint32_t length,
                                                        struct zaclr_array_desc** out_array)
{
    struct zaclr_array_desc* array;
    uint32_t data_size;
    size_t allocation_size;
    struct zaclr_result result;

    if (heap == NULL || out_array == NULL || element_size == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    data_size = element_size * length;
    allocation_size = sizeof(struct zaclr_array_desc) + (size_t)data_size;
    result = zaclr_heap_allocate_object(heap,
                                        allocation_size,
                                        NULL,
                                        type_id,
                                        ZACLR_OBJECT_FAMILY_ARRAY,
                                        zaclr_array_object_flags(element_type),
                                        (struct zaclr_object_desc**)&array);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    array->element_type = element_type;
    array->element_size = element_size;
    array->length = (int32_t)length;
    array->reserved = 0u;
    *out_array = array;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_array_allocate_handle(struct zaclr_heap* heap,
                                                              zaclr_type_id type_id,
                                                              struct zaclr_token element_type,
                                                              uint32_t element_size,
                                                              uint32_t length,
                                                              zaclr_object_handle* out_handle)
{
    struct zaclr_array_desc* array;
    struct zaclr_result result;

    if (out_handle == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    *out_handle = 0u;
    result = zaclr_array_allocate(heap, type_id, element_type, element_size, length, &array);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    *out_handle = zaclr_heap_get_object_handle(heap, &array->object);
    return zaclr_result_ok();
}

extern "C" struct zaclr_array_desc* zaclr_array_from_handle(struct zaclr_heap* heap,
                                                              zaclr_object_handle handle)
{
    return (struct zaclr_array_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" const struct zaclr_array_desc* zaclr_array_from_handle_const(const struct zaclr_heap* heap,
                                                                          zaclr_object_handle handle)
{
    return (const struct zaclr_array_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" void* zaclr_array_data(struct zaclr_array_desc* value)
{
    return value != NULL ? (void*)((uint8_t*)value + sizeof(struct zaclr_array_desc)) : NULL;
}

extern "C" const void* zaclr_array_data_const(const struct zaclr_array_desc* value)
{
    return value != NULL ? (const void*)((const uint8_t*)value + sizeof(struct zaclr_array_desc)) : NULL;
}

extern "C" uint32_t zaclr_array_element_size(const struct zaclr_array_desc* value)
{
    return value != NULL ? value->element_size : 0u;
}

extern "C" struct zaclr_token zaclr_array_element_type(const struct zaclr_array_desc* value)
{
    return value != NULL ? value->element_type : zaclr_token_make(0u);
}
