#include "zaclr_native_System_String.h"

extern "C" {
#include <kernel/console.h>
#include <kernel/support/kernel_memory.h>
}

namespace
{
    static const struct zaclr_string_desc* get_string_arg(struct ::zaclr_runtime* runtime, zaclr_object_handle handle)
    {
        return handle == 0u ? NULL : zaclr_string_from_handle_const(&runtime->heap, handle);
    }

    static struct zaclr_result allocate_string_copy(struct ::zaclr_runtime* runtime,
                                                    const uint16_t* text,
                                                    uint32_t length,
                                                    struct zaclr_native_call_frame& frame)
    {
        zaclr_object_handle handle;
        struct zaclr_result status = zaclr_string_allocate_utf16_handle(&runtime->heap, text, length, &handle);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        return zaclr_native_call_frame_set_string(&frame, handle);
    }

    static zaclr_object_handle stack_object_handle(struct ::zaclr_runtime* runtime, const struct zaclr_stack_value* value)
    {
        if (runtime == NULL || value == NULL || value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        {
            return 0u;
        }

        return zaclr_heap_get_object_handle(&runtime->heap, value->data.object_reference);
    }

    static bool string_starts_with(const struct zaclr_string_desc* text, const struct zaclr_string_desc* prefix)
    {
        uint32_t index;

        if (text == NULL || prefix == NULL)
        {
            return false;
        }

        if (zaclr_string_length(prefix) > zaclr_string_length(text))
        {
            return false;
        }

        for (index = 0u; index < zaclr_string_length(prefix); ++index)
        {
            if (zaclr_string_char_at(text, index) != zaclr_string_char_at(prefix, index))
            {
                return false;
            }
        }

        return true;
    }

    static bool string_ends_with(const struct zaclr_string_desc* text, const struct zaclr_string_desc* suffix)
    {
        uint32_t text_length;
        uint32_t suffix_length;
        uint32_t index;
        uint32_t offset;

        if (text == NULL || suffix == NULL)
        {
            return false;
        }

        text_length = zaclr_string_length(text);
        suffix_length = zaclr_string_length(suffix);
        if (suffix_length > text_length)
        {
            return false;
        }

        offset = text_length - suffix_length;
        for (index = 0u; index < suffix_length; ++index)
        {
            if (zaclr_string_char_at(text, offset + index) != zaclr_string_char_at(suffix, index))
            {
                return false;
            }
        }

        return true;
    }

    static bool string_contains(const struct zaclr_string_desc* text, const struct zaclr_string_desc* value)
    {
        uint32_t text_length;
        uint32_t value_length;
        uint32_t start;
        uint32_t index;

        if (text == NULL || value == NULL)
        {
            return false;
        }

        text_length = zaclr_string_length(text);
        value_length = zaclr_string_length(value);
        if (value_length == 0u)
        {
            return true;
        }

        if (value_length > text_length)
        {
            return false;
        }

        for (start = 0u; start + value_length <= text_length; ++start)
        {
            for (index = 0u; index < value_length; ++index)
            {
                if (zaclr_string_char_at(text, start + index) != zaclr_string_char_at(value, index))
                {
                    break;
                }
            }

            if (index == value_length)
            {
                return true;
            }
        }

        return false;
    }

    static int32_t string_compare_ordinal(const struct zaclr_string_desc* left,
                                          const struct zaclr_string_desc* right)
    {
        uint32_t left_length;
        uint32_t right_length;
        uint32_t min_length;
        uint32_t index;

        if (left == NULL && right == NULL)
        {
            return 0;
        }

        if (left == NULL)
        {
            return -1;
        }

        if (right == NULL)
        {
            return 1;
        }

        left_length = zaclr_string_length(left);
        right_length = zaclr_string_length(right);
        min_length = left_length < right_length ? left_length : right_length;
        for (index = 0u; index < min_length; ++index)
        {
            const uint16_t left_char = zaclr_string_char_at(left, index);
            const uint16_t right_char = zaclr_string_char_at(right, index);
            if (left_char != right_char)
            {
                return left_char < right_char ? -1 : 1;
            }
        }

        if (left_length == right_length)
        {
            return 0;
        }

        return left_length < right_length ? -1 : 1;
    }

    static uint16_t uppercase_ascii(uint16_t value)
    {
        return (value >= (uint16_t)'a' && value <= (uint16_t)'z') ? (uint16_t)(value - 32u) : value;
    }

    static uint16_t lowercase_ascii(uint16_t value)
    {
        return (value >= (uint16_t)'A' && value <= (uint16_t)'Z') ? (uint16_t)(value + 32u) : value;
    }
}

struct zaclr_result zaclr_native_System_String::FastAllocateString___STATIC__STRING__PTR_VALUETYPE_System_Runtime_CompilerServices_MethodTable__I(struct zaclr_native_call_frame& frame)
{
    struct zaclr_result status;
    int64_t length = 0;
    uint16_t* chars = NULL;
    zaclr_object_handle handle = 0u;

    if (frame.runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    status = zaclr_native_call_frame_arg_i8(&frame, 1u, &length);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    if (length < 0 || length > 0x7FFFFFFFll)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    if (length != 0)
    {
        chars = (uint16_t*)kernel_alloc(sizeof(uint16_t) * (size_t)length);
        if (chars == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        kernel_memset(chars, 0, sizeof(uint16_t) * (size_t)length);
    }

    status = zaclr_string_allocate_utf16_handle(&frame.runtime->heap,
                                                chars,
                                                (uint32_t)length,
                                                &handle);
    if (chars != NULL)
    {
        kernel_free(chars);
    }

    return status.status == ZACLR_STATUS_OK ? zaclr_native_call_frame_set_string(&frame, handle) : status;
}

struct zaclr_result zaclr_native_System_String::Concat___STATIC__STRING__STRING__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* left;
    const struct zaclr_string_desc* right;
    uint32_t left_length;
    uint32_t right_length;
    uint32_t index;
    uint16_t* combined;
    struct zaclr_result status;
    zaclr_object_handle left_handle = 0u;
    zaclr_object_handle right_handle = 0u;

    status = zaclr_native_call_frame_arg_object(&frame, 0u, &left_handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_object(&frame, 1u, &right_handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    left = get_string_arg(frame.runtime, left_handle);
    right = get_string_arg(frame.runtime, right_handle);
    left_length = zaclr_string_length(left);
    right_length = zaclr_string_length(right);
    combined = (left_length + right_length) != 0u ? (uint16_t*)kernel_alloc(sizeof(uint16_t) * (left_length + right_length)) : NULL;
    if ((left_length + right_length) != 0u && combined == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    for (index = 0u; index < left_length; ++index)
    {
        combined[index] = zaclr_string_char_at(left, index);
    }

    for (index = 0u; index < right_length; ++index)
    {
        combined[left_length + index] = zaclr_string_char_at(right, index);
    }

    status = allocate_string_copy(frame.runtime, combined, left_length + right_length, frame);
    if (combined != NULL)
    {
        kernel_free(combined);
    }

    return status;
}

struct zaclr_result zaclr_native_System_String::get_Length___I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    struct zaclr_stack_value* this_value = zaclr_native_call_frame_this(&frame);
    console_write("[ZACLR][interop] System.String.get_Length enter\n");
    console_write("[ZACLR][interop] System.String.get_Length has_this=");
    console_write_dec((uint64_t)frame.has_this);
    console_write(" arg_count=");
    console_write_dec((uint64_t)frame.argument_count);
    console_write(" this_ptr=0x");
    console_write_hex64((uint64_t)(uintptr_t)this_value);
    console_write("\n");
    if (frame.runtime == NULL || this_value == NULL || this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    console_write("[ZACLR][interop] System.String.get_Length this_kind=");
    console_write_dec((uint64_t)this_value->kind);
    console_write(" handle=");
    console_write_dec((uint64_t)stack_object_handle(frame.runtime, this_value));
    console_write("\n");

    value = get_string_arg(frame.runtime, stack_object_handle(frame.runtime, this_value));
    console_write("[ZACLR][interop] System.String.get_Length resolved\n");
    return zaclr_native_call_frame_set_i4(&frame, value != NULL ? (int32_t)zaclr_string_length(value) : 0);
}

struct zaclr_result zaclr_native_System_String::get_Chars___CHAR__I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    struct zaclr_result status;
    int32_t index;
    struct zaclr_stack_value* this_value = zaclr_native_call_frame_this(&frame);

    if (frame.runtime == NULL || this_value == NULL || this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    value = get_string_arg(frame.runtime, stack_object_handle(frame.runtime, this_value));
    if (value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    status = zaclr_native_call_frame_arg_i4(&frame, 0u, &index);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    if (index < 0 || (uint32_t)index >= zaclr_string_length(value))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)zaclr_string_char_at(value, (uint32_t)index));
}

struct zaclr_result zaclr_native_System_String::Substring___STRING__I4__I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    int32_t start_index;
    int32_t length;
    uint16_t* chars;
    uint32_t index;
    struct zaclr_result status;
    struct zaclr_stack_value* this_value = zaclr_native_call_frame_this(&frame);

    console_write("[ZACLR][interop] System.String.Substring(I4,I4) enter\n");

    if (frame.runtime == NULL || this_value == NULL || this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    value = get_string_arg(frame.runtime, stack_object_handle(frame.runtime, this_value));
    if (value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    status = zaclr_native_call_frame_arg_i4(&frame, 0u, &start_index);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    console_write("[ZACLR][interop] System.String.Substring(I4,I4) arg0-ok\n");

    length = -1;
    if (zaclr_native_call_frame_argument_count(&frame) == 2u)
    {
        status = zaclr_native_call_frame_arg_i4(&frame, 1u, &length);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        console_write("[ZACLR][interop] System.String.Substring(I4,I4) arg1-ok\n");
    }

    if (start_index < 0 || (uint32_t)start_index > zaclr_string_length(value))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    if (length < 0)
    {
        length = (int32_t)zaclr_string_length(value) - start_index;
    }

    if (length < 0 || ((uint32_t)start_index + (uint32_t)length) > zaclr_string_length(value))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    chars = length != 0 ? (uint16_t*)kernel_alloc(sizeof(uint16_t) * (uint32_t)length) : NULL;
    if (length != 0 && chars == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    for (index = 0u; index < (uint32_t)length; ++index)
    {
        chars[index] = zaclr_string_char_at(value, (uint32_t)start_index + index);
    }

    status = allocate_string_copy(frame.runtime, chars, (uint32_t)length, frame);
    if (chars != NULL)
    {
        kernel_free(chars);
    }

    console_write("[ZACLR][interop] System.String.Substring(I4,I4) return\n");

    return status;
}

struct zaclr_result zaclr_native_System_String::Substring___STRING__I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    struct zaclr_result status;
    int32_t start_index;
    int32_t length;
    uint16_t* chars;
    uint32_t index;
    struct zaclr_stack_value* this_value = zaclr_native_call_frame_this(&frame);
    struct zaclr_stack_value* start_value = zaclr_native_call_frame_arg(&frame, 0u);

    console_write("[ZACLR][interop] System.String.Substring(I4) enter\n");
    if (this_value != NULL)
    {
        console_write("[ZACLR][interop] System.String.Substring(I4) this-kind=");
        console_write_dec((uint64_t)this_value->kind);
        console_write("\n");
    }
    if (start_value != NULL)
    {
        console_write("[ZACLR][interop] System.String.Substring(I4) arg-kind=");
        console_write_dec((uint64_t)start_value->kind);
        console_write("\n");
    }

    if (frame.runtime == NULL || this_value == NULL || start_value == NULL || this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE || start_value->kind != ZACLR_STACK_VALUE_I4)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    console_write("[ZACLR][interop] System.String.Substring(I4) args-ok\n");

    value = get_string_arg(frame.runtime, stack_object_handle(frame.runtime, this_value));
    if (value == NULL)
    {
        return zaclr_native_call_frame_set_object(&frame, 0u);
    }

    start_index = start_value->data.i4;
    if (start_index < 0 || (size_t)start_index > zaclr_string_length(value))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    console_write("[ZACLR][interop] System.String.Substring(I4) range-ok\n");

    length = (int32_t)(zaclr_string_length(value) - (size_t)start_index);
    chars = length != 0 ? (uint16_t*)kernel_alloc(sizeof(uint16_t) * (uint32_t)length) : NULL;
    if (length != 0 && chars == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    for (index = 0u; index < (uint32_t)length; ++index)
    {
        chars[index] = zaclr_string_char_at(value, (uint32_t)start_index + index);
    }

    status = allocate_string_copy(frame.runtime, chars, (uint32_t)length, frame);
    if (chars != NULL)
    {
        kernel_free(chars);
    }

    console_write("[ZACLR][interop] System.String.Substring(I4) return\n");

    return status;
}

struct zaclr_result zaclr_native_System_String::StartsWith___BOOLEAN__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    const struct zaclr_string_desc* prefix;
    struct zaclr_stack_value* this_value = zaclr_native_call_frame_this(&frame);
    struct zaclr_result status;

    if (frame.runtime == NULL || this_value == NULL || this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    value = get_string_arg(frame.runtime, stack_object_handle(frame.runtime, this_value));
    status = zaclr_native_call_frame_arg_string(&frame, 0u, &prefix);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    return zaclr_native_call_frame_set_bool(&frame, string_starts_with(value, prefix));
}

struct zaclr_result zaclr_native_System_String::EndsWith___BOOLEAN__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    const struct zaclr_string_desc* suffix;
    struct zaclr_stack_value* this_value = zaclr_native_call_frame_this(&frame);
    struct zaclr_result status;

    if (frame.runtime == NULL || this_value == NULL || this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    value = get_string_arg(frame.runtime, stack_object_handle(frame.runtime, this_value));
    status = zaclr_native_call_frame_arg_string(&frame, 0u, &suffix);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    return zaclr_native_call_frame_set_bool(&frame, string_ends_with(value, suffix));
}

struct zaclr_result zaclr_native_System_String::Contains___BOOLEAN__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    const struct zaclr_string_desc* needle;
    struct zaclr_stack_value* this_value = zaclr_native_call_frame_this(&frame);
    struct zaclr_result status;

    if (frame.runtime == NULL || this_value == NULL || this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    value = get_string_arg(frame.runtime, stack_object_handle(frame.runtime, this_value));
    status = zaclr_native_call_frame_arg_string(&frame, 0u, &needle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    return zaclr_native_call_frame_set_bool(&frame, string_contains(value, needle));
}

struct zaclr_result zaclr_native_System_String::Compare___STATIC__I4__STRING__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* left;
    const struct zaclr_string_desc* right;
    struct zaclr_result status = zaclr_native_call_frame_arg_string(&frame, 0u, &left);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_string(&frame, 1u, &right);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    return zaclr_native_call_frame_set_i4(&frame, string_compare_ordinal(left, right));
}

struct zaclr_result zaclr_native_System_String::ToUpper___STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    uint16_t* converted;
    uint32_t index;
    struct zaclr_stack_value* this_value = zaclr_native_call_frame_this(&frame);
    struct zaclr_result status;

    if (frame.runtime == NULL || this_value == NULL || this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    value = get_string_arg(frame.runtime, stack_object_handle(frame.runtime, this_value));
    if (value == NULL)
    {
        return zaclr_native_call_frame_set_string(&frame, 0u);
    }

    converted = zaclr_string_length(value) != 0u ? (uint16_t*)kernel_alloc(sizeof(uint16_t) * zaclr_string_length(value)) : NULL;
    if (zaclr_string_length(value) != 0u && converted == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    for (index = 0u; index < zaclr_string_length(value); ++index)
    {
        converted[index] = uppercase_ascii(zaclr_string_char_at(value, index));
    }

    status = allocate_string_copy(frame.runtime, converted, zaclr_string_length(value), frame);
    if (converted != NULL)
    {
        kernel_free(converted);
    }

    return status;
}

struct zaclr_result zaclr_native_System_String::ToLower___STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    uint16_t* converted;
    uint32_t index;
    struct zaclr_stack_value* this_value = zaclr_native_call_frame_this(&frame);
    struct zaclr_result status;

    if (frame.runtime == NULL || this_value == NULL || this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    value = get_string_arg(frame.runtime, stack_object_handle(frame.runtime, this_value));
    if (value == NULL)
    {
        return zaclr_native_call_frame_set_string(&frame, 0u);
    }

    converted = zaclr_string_length(value) != 0u ? (uint16_t*)kernel_alloc(sizeof(uint16_t) * zaclr_string_length(value)) : NULL;
    if (zaclr_string_length(value) != 0u && converted == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    for (index = 0u; index < zaclr_string_length(value); ++index)
    {
        converted[index] = lowercase_ascii(zaclr_string_char_at(value, index));
    }

    status = allocate_string_copy(frame.runtime, converted, zaclr_string_length(value), frame);
    if (converted != NULL)
    {
        kernel_free(converted);
    }

    return status;
}

struct zaclr_result zaclr_native_System_String::Trim___STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    uint32_t start;
    uint32_t end;
    uint32_t index;
    uint16_t* trimmed;
    struct zaclr_stack_value* this_value = zaclr_native_call_frame_this(&frame);
    struct zaclr_result status;

    if (frame.runtime == NULL || this_value == NULL || this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    value = get_string_arg(frame.runtime, stack_object_handle(frame.runtime, this_value));
    if (value == NULL)
    {
        return zaclr_native_call_frame_set_string(&frame, 0u);
    }

    start = 0u;
    end = zaclr_string_length(value);
    while (start < end && zaclr_string_char_at(value, start) == (uint16_t)' ')
    {
        ++start;
    }
    while (end > start && zaclr_string_char_at(value, end - 1u) == (uint16_t)' ')
    {
        --end;
    }

    trimmed = (end - start) != 0u ? (uint16_t*)kernel_alloc(sizeof(uint16_t) * (end - start)) : NULL;
    if ((end - start) != 0u && trimmed == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    for (index = 0u; index < (end - start); ++index)
    {
        trimmed[index] = zaclr_string_char_at(value, start + index);
    }

    status = allocate_string_copy(frame.runtime, trimmed, end - start, frame);
    if (trimmed != NULL)
    {
        kernel_free(trimmed);
    }

    return status;
}

struct zaclr_result zaclr_native_System_String::Replace___STRING__STRING__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    const struct zaclr_string_desc* old_value;
    const struct zaclr_string_desc* new_value;
    uint32_t capacity;
    uint16_t* buffer;
    uint32_t out_length;
    uint32_t index;
    struct zaclr_stack_value* this_value = zaclr_native_call_frame_this(&frame);
    struct zaclr_result status;

    if (frame.runtime == NULL || this_value == NULL || this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    value = get_string_arg(frame.runtime, stack_object_handle(frame.runtime, this_value));
    status = zaclr_native_call_frame_arg_string(&frame, 0u, &old_value);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }
    status = zaclr_native_call_frame_arg_string(&frame, 1u, &new_value);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    if (value == NULL || old_value == NULL || new_value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    if (zaclr_string_length(old_value) == 0u)
    {
        return allocate_string_copy(frame.runtime, zaclr_string_code_units(value), zaclr_string_length(value), frame);
    }

    capacity = (zaclr_string_length(value) + 1u) * (zaclr_string_length(new_value) + 1u);
    buffer = capacity != 0u ? (uint16_t*)kernel_alloc(sizeof(uint16_t) * capacity) : NULL;
    if (capacity != 0u && buffer == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    out_length = 0u;
    index = 0u;
    while (index < zaclr_string_length(value))
    {
        uint32_t match_index;
        bool matched = true;
        if (index + zaclr_string_length(old_value) <= zaclr_string_length(value))
        {
            for (match_index = 0u; match_index < zaclr_string_length(old_value); ++match_index)
            {
                if (zaclr_string_char_at(value, index + match_index) != zaclr_string_char_at(old_value, match_index))
                {
                    matched = false;
                    break;
                }
            }
        }
        else
        {
            matched = false;
        }

        if (matched)
        {
            for (match_index = 0u; match_index < zaclr_string_length(new_value); ++match_index)
            {
                buffer[out_length++] = zaclr_string_char_at(new_value, match_index);
            }
            index += zaclr_string_length(old_value);
        }
        else
        {
            buffer[out_length++] = zaclr_string_char_at(value, index);
            ++index;
        }
    }

    status = allocate_string_copy(frame.runtime, buffer, out_length, frame);
    if (buffer != NULL)
    {
        kernel_free(buffer);
    }

    return status;
}

struct zaclr_result zaclr_native_System_String::IsNullOrEmpty___STATIC__BOOLEAN__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    struct zaclr_result status = zaclr_native_call_frame_arg_string(&frame, 0u, &value);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    return zaclr_native_call_frame_set_bool(&frame, value == NULL || zaclr_string_length(value) == 0u);
}

struct zaclr_result zaclr_native_System_String::IndexOf___I4__CHAR(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* value;
    int32_t needle;
    uint32_t index;
    struct zaclr_stack_value* this_value = zaclr_native_call_frame_this(&frame);
    struct zaclr_result status;

    if (frame.runtime == NULL || this_value == NULL || this_value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    value = get_string_arg(frame.runtime, stack_object_handle(frame.runtime, this_value));
    if (value == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    status = zaclr_native_call_frame_arg_i4(&frame, 0u, &needle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    for (index = 0u; index < zaclr_string_length(value); ++index)
    {
        if ((int32_t)zaclr_string_char_at(value, index) == needle)
        {
            return zaclr_native_call_frame_set_i4(&frame, (int32_t)index);
        }
    }

    return zaclr_native_call_frame_set_i4(&frame, -1);
}

struct zaclr_result zaclr_native_System_String::_ctor___VOID__SZARRAY_CHAR__I4__I4(struct zaclr_native_call_frame& frame)
{
    uint16_t* chars = NULL;
    uint32_t length = 0u;
    uint32_t index;
    struct zaclr_result status;
    int32_t first_i4 = 0;
    int32_t second_i4 = 0;

    if (frame.runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    if (zaclr_native_call_frame_argument_count(&frame) == 2u)
    {
        status = zaclr_native_call_frame_arg_i4(&frame, 0u, &first_i4);
        if (status.status != ZACLR_STATUS_OK) return status;
        status = zaclr_native_call_frame_arg_i4(&frame, 1u, &second_i4);
        if (status.status != ZACLR_STATUS_OK) return status;
        if (second_i4 < 0) return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);

        length = (uint32_t)second_i4;
        chars = length != 0u ? (uint16_t*)kernel_alloc(sizeof(uint16_t) * length) : NULL;
        if (length != 0u && chars == NULL) return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
        for (index = 0u; index < length; ++index)
        {
            chars[index] = (uint16_t)first_i4;
        }
    }
    else if (zaclr_native_call_frame_argument_count(&frame) == 1u || zaclr_native_call_frame_argument_count(&frame) == 3u)
    {
        zaclr_object_handle array_handle = 0u;
        const struct zaclr_array_desc* array;
        const uint8_t* data;
        uint32_t element_size;
        int32_t start_index = 0;
        int32_t count;

        status = zaclr_native_call_frame_arg_object(&frame, 0u, &array_handle);
        if (status.status != ZACLR_STATUS_OK) return status;
        array = zaclr_array_from_handle_const(&frame.runtime->heap, array_handle);
        if (array == NULL) return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);

        data = (const uint8_t*)zaclr_array_data_const(array);
        element_size = zaclr_array_element_size(array);
        count = (int32_t)zaclr_array_length(array);
        if (element_size < 2u || data == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if (zaclr_native_call_frame_argument_count(&frame) == 3u)
        {
            status = zaclr_native_call_frame_arg_i4(&frame, 1u, &start_index);
            if (status.status != ZACLR_STATUS_OK) return status;
            status = zaclr_native_call_frame_arg_i4(&frame, 2u, &count);
            if (status.status != ZACLR_STATUS_OK) return status;
        }

        if (start_index < 0 || count < 0 || ((uint32_t)start_index + (uint32_t)count) > zaclr_array_length(array))
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        length = (uint32_t)count;
        chars = length != 0u ? (uint16_t*)kernel_alloc(sizeof(uint16_t) * length) : NULL;
        if (length != 0u && chars == NULL) return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
        for (index = 0u; index < length; ++index)
        {
            const size_t offset = ((size_t)start_index + index) * element_size;
            chars[index] = (uint16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1u] << 8));
        }
    }
    else
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    {
        zaclr_object_handle handle = 0u;
        status = zaclr_string_allocate_utf16_handle(&frame.runtime->heap, chars, length, &handle);
        if (chars != NULL)
        {
            kernel_free(chars);
        }

        return status.status == ZACLR_STATUS_OK ? zaclr_native_call_frame_set_object(&frame, handle) : status;
    }
}

struct zaclr_result zaclr_native_System_String::_ctor___VOID__CHAR__I4(struct zaclr_native_call_frame& frame)
{
    return _ctor___VOID__SZARRAY_CHAR__I4__I4(frame);
}

struct zaclr_result zaclr_native_System_String::_ctor___VOID__SZARRAY_CHAR(struct zaclr_native_call_frame& frame)
{
    return _ctor___VOID__SZARRAY_CHAR__I4__I4(frame);
}

struct zaclr_result zaclr_native_System_String::get_Empty___STATIC__STRING(struct zaclr_native_call_frame& frame)
{
    static const uint16_t empty_text[1] = { 0u };
    if (frame.runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    return allocate_string_copy(frame.runtime, empty_text, 0u, frame);
}

struct zaclr_result zaclr_native_System_String::op_Equality___STATIC__BOOLEAN__STRING__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_string_desc* left;
    const struct zaclr_string_desc* right;
    zaclr_object_handle left_handle = 0u;
    zaclr_object_handle right_handle = 0u;
    struct zaclr_result status = zaclr_native_call_frame_arg_object(&frame, 0u, &left_handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    status = zaclr_native_call_frame_arg_object(&frame, 1u, &right_handle);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    if (left_handle == right_handle)
    {
        return zaclr_native_call_frame_set_bool(&frame, true);
    }

    left = get_string_arg(frame.runtime, left_handle);
    right = get_string_arg(frame.runtime, right_handle);
    return zaclr_native_call_frame_set_bool(&frame, string_compare_ordinal(left, right) == 0);
}
