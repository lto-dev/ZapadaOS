#include <kernel/zaclr/heap/zaclr_string.h>

#include <kernel/zaclr/loader/zaclr_assembly_registry.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_type_prepare.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

extern "C" {
#include <kernel/support/kernel_memory.h>
}

namespace
{
    static const uint16_t* string_code_units_storage(const struct zaclr_string_desc* value)
    {
        return value != NULL ? &value->first_char : NULL;
    }

    static char* string_ascii_storage(struct zaclr_string_desc* value)
    {
        if (value == NULL)
        {
            return NULL;
        }

        return (char*)((uint8_t*)&value->first_char + ((size_t)zaclr_string_length(value) * sizeof(uint16_t)));
    }

    static const char* string_ascii_storage_const(const struct zaclr_string_desc* value)
    {
        return string_ascii_storage((struct zaclr_string_desc*)value);
    }
}

extern "C" struct zaclr_result zaclr_string_allocate_ascii(struct zaclr_heap* heap,
                                                               const char* text,
                                                               uint32_t length,
                                                               struct zaclr_string_desc** out_string)
{
    uint16_t* utf16_text;
    uint32_t index;
    struct zaclr_result result;

    if (heap == NULL || out_string == NULL || (text == NULL && length != 0u))
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

    result = zaclr_string_allocate_utf16(heap, utf16_text, length, out_string);
    if (utf16_text != NULL)
    {
        kernel_free(utf16_text);
    }

    return result;
}

extern "C" struct zaclr_result zaclr_string_allocate_utf16(struct zaclr_heap* heap,
                                                                const uint16_t* text,
                                                                uint32_t length,
                                                                struct zaclr_string_desc** out_string)
{
    struct zaclr_string_desc* value;
    size_t allocation_size;
    struct zaclr_result result;
    uint16_t* utf16_chars;
    uint32_t index;
    struct zaclr_method_table* method_table = NULL;

    if (heap == NULL || out_string == NULL || (text == NULL && length != 0u))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    allocation_size = sizeof(struct zaclr_string_desc);
    if (length > 1u)
    {
        allocation_size += ((size_t)length - 1u) * sizeof(uint16_t);
    }
    allocation_size += (size_t)length + 1u;

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

    if (heap->runtime != NULL)
    {
        const struct zaclr_app_domain* domain = zaclr_runtime_current_domain(heap->runtime);
        const struct zaclr_loaded_assembly* corelib = NULL;
        if (domain != NULL)
        {
            corelib = zaclr_assembly_registry_find_by_name(&domain->registry, "System.Private.CoreLib");
        }
        if (corelib != NULL)
        {
            struct zaclr_member_name_ref type_name = {"System", "String", NULL};
            const struct zaclr_type_desc* type_desc = zaclr_type_system_find_type_by_name(corelib, &type_name);
            if (type_desc != NULL)
            {
                (void)zaclr_type_prepare(heap->runtime,
                                         (struct zaclr_loaded_assembly*)corelib,
                                         type_desc,
                                         &method_table);
            }
        }
    }

    value->object.header.method_table = method_table;
    value->length = (int32_t)length;
    value->first_char = 0u;
    utf16_chars = (uint16_t*)&value->first_char;
    for (index = 0u; index < length; ++index)
    {
        utf16_chars[index] = text[index];
        string_ascii_storage(value)[index] = (char)(uint8_t)text[index];
    }
    string_ascii_storage(value)[length] = '\0';

    *out_string = value;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_string_allocate_ascii_handle(struct zaclr_heap* heap,
                                                                     const char* text,
                                                                     uint32_t length,
                                                                     zaclr_object_handle* out_handle)
{
    struct zaclr_string_desc* value;
    struct zaclr_result result;

    if (out_handle == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    *out_handle = 0u;
    result = zaclr_string_allocate_ascii(heap, text, length, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    *out_handle = zaclr_heap_get_object_handle(heap, &value->object);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_string_allocate_utf16_handle(struct zaclr_heap* heap,
                                                                     const uint16_t* text,
                                                                     uint32_t length,
                                                                     zaclr_object_handle* out_handle)
{
    struct zaclr_string_desc* value;
    struct zaclr_result result;

    if (out_handle == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    *out_handle = 0u;
    result = zaclr_string_allocate_utf16(heap, text, length, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    *out_handle = zaclr_heap_get_object_handle(heap, &value->object);
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

extern "C" const uint16_t* zaclr_string_chars(const struct zaclr_string_desc* value)
{
    return string_code_units_storage(value);
}

extern "C" const uint16_t* zaclr_string_chars_from_handle(const struct zaclr_heap* heap,
                                                           zaclr_object_handle handle)
{
    const struct zaclr_string_desc* value = zaclr_string_from_handle_const(heap, handle);
    return zaclr_string_chars(value);
}

extern "C" const char* zaclr_string_ascii_chars(const struct zaclr_string_desc* value)
{
    return string_ascii_storage_const(value);
}

extern "C" const char* zaclr_string_ascii_chars_from_handle(const struct zaclr_heap* heap,
                                                             zaclr_object_handle handle)
{
    const struct zaclr_string_desc* value = zaclr_string_from_handle_const(heap, handle);
    return zaclr_string_ascii_chars(value);
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

extern "C" uint32_t zaclr_string_char_count(const struct zaclr_string_desc* value)
{
    return value != NULL && value->length > 0 ? (uint32_t)value->length : 0u;
}

extern "C" uint32_t zaclr_string_length(const struct zaclr_string_desc* value)
{
    return zaclr_string_char_count(value);
}
