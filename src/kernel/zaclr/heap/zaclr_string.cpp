#include <kernel/zaclr/heap/zaclr_string.h>

extern "C" {
#include <kernel/support/kernel_memory.h>
}

namespace
{
    static const uint16_t* string_code_units_storage(const struct zaclr_string_desc* value)
    {
        return value != NULL ? (const uint16_t*)(value + 1) : NULL;
    }

    static char* string_narrow_storage(struct zaclr_string_desc* value)
    {
        return value != NULL
            ? (char*)((uint8_t*)(value + 1) + ((size_t)value->length * sizeof(uint16_t)))
            : NULL;
    }

    static const char* string_narrow_storage_const(const struct zaclr_string_desc* value)
    {
        return value != NULL
            ? (const char*)((const uint8_t*)(value + 1) + ((size_t)value->length * sizeof(uint16_t)))
            : NULL;
    }
}

extern "C" struct zaclr_result zaclr_string_allocate_ascii(struct zaclr_heap* heap,
                                                              const char* text,
                                                              uint32_t length,
                                                              zaclr_object_handle* out_handle)
{
    uint16_t* utf16_text;
    uint32_t index;
    struct zaclr_result result;

    if (heap == NULL || out_handle == NULL || (text == NULL && length != 0u))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    utf16_text = length != 0u ? (uint16_t*)kernel_alloc(sizeof(uint16_t) * length) : NULL;
    if (length != 0u && utf16_text == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_HEAP);
    }

    for (index = 0u; index < length; ++index)
    {
        utf16_text[index] = (uint16_t)(uint8_t)text[index];
    }

    result = zaclr_string_allocate_utf16(heap, utf16_text, length, out_handle);
    if (utf16_text != NULL)
    {
        kernel_free(utf16_text);
    }

    return result;
}

extern "C" struct zaclr_result zaclr_string_allocate_utf16(struct zaclr_heap* heap,
                                                               const uint16_t* text,
                                                               uint32_t length,
                                                               zaclr_object_handle* out_handle)
{
    struct zaclr_string_desc* value;
    size_t allocation_size;
    struct zaclr_result result;
    uint16_t* utf16_chars;
    char* narrow_chars;
    uint32_t index;

    if (heap == NULL || out_handle == NULL || (text == NULL && length != 0u))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    allocation_size = sizeof(struct zaclr_string_desc)
                    + ((size_t)length * sizeof(uint16_t))
                    + (size_t)length
                    + 1u;
    result = zaclr_heap_allocate_object(heap,
                                        allocation_size,
                                        NULL,
                                        0u,
                                        ZACLR_OBJECT_FAMILY_STRING,
                                        ZACLR_OBJECT_FLAG_STRING | ZACLR_OBJECT_FLAG_REFERENCE_TYPE,
                                        (struct zaclr_object_desc**)&value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    value->length = length;
    value->byte_length = length * (uint32_t)sizeof(uint16_t);
    utf16_chars = (uint16_t*)(value + 1);
    narrow_chars = string_narrow_storage(value);
    for (index = 0u; index < length; ++index)
    {
        const uint16_t code_unit = text[index];
        utf16_chars[index] = code_unit;
        narrow_chars[index] = (char)(uint8_t)code_unit;
    }

    narrow_chars[length] = '\0';
    *out_handle = value->object.handle;
    return zaclr_result_ok();
}

extern "C" struct zaclr_string_desc* zaclr_string_from_handle(struct zaclr_heap* heap,
                                                                 zaclr_object_handle handle)
{
    return (struct zaclr_string_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" const struct zaclr_string_desc* zaclr_string_from_handle_const(const struct zaclr_heap* heap,
                                                                              zaclr_object_handle handle)
{
    return (const struct zaclr_string_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" const char* zaclr_string_chars(const struct zaclr_string_desc* value)
{
    return string_narrow_storage_const(value);
}

extern "C" const char* zaclr_string_chars_from_handle(const struct zaclr_heap* heap,
                                                         zaclr_object_handle handle)
{
    const struct zaclr_string_desc* value = zaclr_string_from_handle_const(heap, handle);
    return zaclr_string_chars(value);
}

extern "C" const uint16_t* zaclr_string_code_units(const struct zaclr_string_desc* value)
{
    return string_code_units_storage(value);
}

extern "C" uint16_t zaclr_string_char_at(const struct zaclr_string_desc* value,
                                            uint32_t index)
{
    const uint16_t* code_units = string_code_units_storage(value);
    return (code_units != NULL && index < zaclr_string_length(value)) ? code_units[index] : 0u;
}

extern "C" uint32_t zaclr_string_length(const struct zaclr_string_desc* value)
{
    return value != NULL ? value->length : 0u;
}
