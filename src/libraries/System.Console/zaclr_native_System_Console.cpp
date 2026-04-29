#include <kernel/zaclr/heap/zaclr_array.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>
#include <kernel/zaclr/interop/zaclr_marshalling.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_method_table.h>

#include "zaclr_native_System_Console.h"

extern "C" {
#include <kernel/console.h>
#include <kernel/support/kernel_memory.h>
}

namespace
{
    static void console_write_ascii_char(uint16_t value)
    {
        char text[2];
        text[0] = value == '\n' || value == '\r' || value == '\t' || (value >= 32u && value <= 126u)
            ? (char)(uint8_t)value
            : '?';
        text[1] = '\0';
        console_write(text);
    }

    static void console_write_utf16_text(const uint16_t* text, uint32_t length)
    {
        uint32_t index;

        if (text == NULL)
        {
            return;
        }

        for (index = 0u; index < length; ++index)
        {
            console_write_ascii_char(text[index]);
        }
    }

    static void console_write_signed_i64(int64_t value)
    {
        if (value < 0)
        {
            console_write("-");
            console_write_dec((uint64_t)(-(value + 1)) + 1u);
            return;
        }

        console_write_dec((uint64_t)value);
    }

    static struct zaclr_result console_set_void(struct zaclr_native_call_frame& frame, bool append_newline)
    {
        if (append_newline)
        {
            console_write("\n");
        }

        return zaclr_native_call_frame_set_void(&frame);
    }

    static struct zaclr_result console_write_string_arg(struct zaclr_native_call_frame& frame,
                                                        uint32_t argument_index,
                                                        bool append_newline)
    {
        zaclr_object_handle string_handle;
        struct zaclr_result status;

        if (frame.runtime == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        status = zaclr_native_call_frame_arg_object(&frame, argument_index, &string_handle);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        if (string_handle != 0u)
        {
            const struct zaclr_string_desc* text = zaclr_string_from_handle_const(&frame.runtime->heap, string_handle);
            if (text == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
            }

            console_write_utf16_text(zaclr_string_code_units(text), zaclr_string_length(text));
        }

        return console_set_void(frame, append_newline);
    }

    static struct zaclr_result console_write_bool_arg(struct zaclr_native_call_frame& frame,
                                                      bool append_newline)
    {
        bool value;
        struct zaclr_result status = zaclr_native_call_frame_arg_bool(&frame, 0u, &value);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        console_write(value ? "True" : "False");
        return console_set_void(frame, append_newline);
    }

    static struct zaclr_result console_write_char_arg(struct zaclr_native_call_frame& frame,
                                                      bool append_newline)
    {
        int32_t value;
        struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &value);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        console_write_ascii_char((uint16_t)value);
        return console_set_void(frame, append_newline);
    }

    static struct zaclr_result console_write_char_array_range(struct zaclr_native_call_frame& frame,
                                                              bool has_range,
                                                              bool append_newline)
    {
        const struct zaclr_array_desc* array;
        const uint8_t* data;
        uint32_t element_size;
        uint32_t array_length;
        int32_t index = 0;
        int32_t count;
        uint32_t cursor;
        struct zaclr_result status = zaclr_native_call_frame_arg_array(&frame, 0u, &array);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        if (array == NULL)
        {
            return console_set_void(frame, append_newline);
        }

        data = (const uint8_t*)zaclr_array_data_const(array);
        element_size = zaclr_array_element_size(array);
        array_length = zaclr_array_length(array);
        count = (int32_t)array_length;
        if (data == NULL || element_size < 2u)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if (has_range)
        {
            status = zaclr_native_call_frame_arg_i4(&frame, 1u, &index);
            if (status.status != ZACLR_STATUS_OK)
            {
                return status;
            }

            status = zaclr_native_call_frame_arg_i4(&frame, 2u, &count);
            if (status.status != ZACLR_STATUS_OK)
            {
                return status;
            }
        }

        if (index < 0 || count < 0 || ((uint32_t)index + (uint32_t)count) > array_length)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        for (cursor = 0u; cursor < (uint32_t)count; ++cursor)
        {
            size_t offset = ((size_t)(uint32_t)index + cursor) * element_size;
            uint16_t value = (uint16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1u] << 8));
            console_write_ascii_char(value);
        }

        return console_set_void(frame, append_newline);
    }

    static struct zaclr_result console_write_i4_arg(struct zaclr_native_call_frame& frame,
                                                    bool append_newline)
    {
        int32_t value;
        struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &value);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        console_write_signed_i64((int64_t)value);
        return console_set_void(frame, append_newline);
    }

    static struct zaclr_result console_write_u4_arg(struct zaclr_native_call_frame& frame,
                                                    bool append_newline)
    {
        int32_t value;
        struct zaclr_result status = zaclr_native_call_frame_arg_i4(&frame, 0u, &value);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        console_write_dec((uint64_t)(uint32_t)value);
        return console_set_void(frame, append_newline);
    }

    static struct zaclr_result console_write_i8_arg(struct zaclr_native_call_frame& frame,
                                                    bool append_newline)
    {
        int64_t value;
        struct zaclr_result status = zaclr_native_call_frame_arg_i8(&frame, 0u, &value);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        console_write_signed_i64(value);
        return console_set_void(frame, append_newline);
    }

    static struct zaclr_result console_write_u8_arg(struct zaclr_native_call_frame& frame,
                                                    bool append_newline)
    {
        int64_t value;
        struct zaclr_result status = zaclr_native_call_frame_arg_i8(&frame, 0u, &value);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        console_write_dec((uint64_t)value);
        return console_set_void(frame, append_newline);
    }

    static uint64_t stack_value_unsigned_bits(const struct zaclr_stack_value* value)
    {
        if (value == NULL)
        {
            return 0u;
        }

        switch (value->kind)
        {
            case ZACLR_STACK_VALUE_I4:
                return (uint64_t)(uint32_t)value->data.i4;
            case ZACLR_STACK_VALUE_I8:
                return (uint64_t)value->data.i8;
            case ZACLR_STACK_VALUE_R4:
                return (uint64_t)value->data.r4_bits;
            case ZACLR_STACK_VALUE_R8:
                return value->data.r8_bits;
            case ZACLR_STACK_VALUE_VALUETYPE:
            {
                const uint8_t* payload = (const uint8_t*)zaclr_stack_value_payload_const(value);
                uint32_t count = value->payload_size > 8u ? 8u : value->payload_size;
                uint32_t index;
                uint64_t result = 0u;
                if (payload == NULL)
                {
                    return 0u;
                }

                for (index = 0u; index < count; ++index)
                {
                    result |= ((uint64_t)payload[index]) << (index * 8u);
                }

                return result;
            }
            default:
                return 0u;
        }
    }

    static struct zaclr_result console_write_r4_arg(struct zaclr_native_call_frame& frame,
                                                    bool append_newline)
    {
        struct zaclr_stack_value* value = zaclr_native_call_frame_arg(&frame, 0u);
        if (value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        console_write("<r4:0x");
        console_write_hex64(stack_value_unsigned_bits(value));
        console_write(">");
        return console_set_void(frame, append_newline);
    }

    static struct zaclr_result console_write_r8_arg(struct zaclr_native_call_frame& frame,
                                                    bool append_newline)
    {
        struct zaclr_stack_value* value = zaclr_native_call_frame_arg(&frame, 0u);
        if (value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        console_write("<r8:0x");
        console_write_hex64(stack_value_unsigned_bits(value));
        console_write(">");
        return console_set_void(frame, append_newline);
    }

    static struct zaclr_result console_write_decimal_arg(struct zaclr_native_call_frame& frame,
                                                         bool append_newline)
    {
        struct zaclr_stack_value* value = zaclr_native_call_frame_arg(&frame, 0u);
        const uint8_t* payload;
        uint32_t index;
        if (value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        payload = (const uint8_t*)zaclr_stack_value_payload_const(value);
        if (payload == NULL)
        {
            console_write("0");
            return console_set_void(frame, append_newline);
        }

        console_write("<decimal:0x");
        for (index = value->payload_size; index > 0u; --index)
        {
            static const char hex[] = "0123456789ABCDEF";
            uint8_t byte = payload[index - 1u];
            char text[3];
            text[0] = hex[(byte >> 4) & 0x0Fu];
            text[1] = hex[byte & 0x0Fu];
            text[2] = '\0';
            console_write(text);
        }
        console_write(">");
        return console_set_void(frame, append_newline);
    }

    static bool boxed_value_write(const struct zaclr_boxed_value_desc* boxed)
    {
        if (boxed == NULL)
        {
            return false;
        }

        switch (boxed->value.kind)
        {
            case ZACLR_STACK_VALUE_I4:
                console_write_signed_i64((int64_t)boxed->value.data.i4);
                return true;
            case ZACLR_STACK_VALUE_I8:
                console_write_signed_i64(boxed->value.data.i8);
                return true;
            case ZACLR_STACK_VALUE_R4:
                console_write("<r4:0x");
                console_write_hex64((uint64_t)boxed->value.data.r4_bits);
                console_write(">");
                return true;
            case ZACLR_STACK_VALUE_R8:
                console_write("<r8:0x");
                console_write_hex64(boxed->value.data.r8_bits);
                console_write(">");
                return true;
            default:
                return false;
        }
    }

    static struct zaclr_result console_write_object_at(struct zaclr_native_call_frame& frame,
                                                       uint32_t argument_index)
    {
        zaclr_object_handle handle;
        struct zaclr_object_desc* object;
        struct zaclr_result status;

        if (frame.runtime == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        status = zaclr_native_call_frame_arg_object(&frame, argument_index, &handle);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        if (handle == 0u)
        {
            return zaclr_result_ok();
        }

        object = zaclr_heap_get_object(&frame.runtime->heap, handle);
        if (object == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
        }

        if (zaclr_object_family(object) == ZACLR_OBJECT_FAMILY_STRING)
        {
            const struct zaclr_string_desc* text = (const struct zaclr_string_desc*)object;
            console_write_utf16_text(zaclr_string_code_units(text), zaclr_string_length(text));
            return zaclr_result_ok();
        }

        if ((zaclr_object_flags(object) & ZACLR_OBJECT_FLAG_BOXED_VALUE) != 0u
            && boxed_value_write((const struct zaclr_boxed_value_desc*)object))
        {
            return zaclr_result_ok();
        }

        console_write(object->header.method_table != NULL
            && object->header.method_table->type_desc != NULL
            && object->header.method_table->type_desc->type_name.text != NULL
            ? object->header.method_table->type_desc->type_name.text
            : "<object>");
        return zaclr_result_ok();
    }

    static struct zaclr_result console_write_object_arg(struct zaclr_native_call_frame& frame,
                                                        bool append_newline)
    {
        struct zaclr_result status = console_write_object_at(frame, 0u);
        return status.status == ZACLR_STATUS_OK ? console_set_void(frame, append_newline) : status;
    }

    static struct zaclr_result console_write_format_object(struct zaclr_native_call_frame& frame,
                                                           uint32_t argument_index)
    {
        return console_write_object_at(frame, argument_index);
    }

    static struct zaclr_result console_write_format_array(struct zaclr_native_call_frame& frame,
                                                          uint32_t argument_index)
    {
        const struct zaclr_array_desc* array;
        const uint8_t* data;
        uint32_t element_size;
        uint32_t index;
        struct zaclr_result status = zaclr_native_call_frame_arg_array(&frame, argument_index, &array);
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        if (array == NULL)
        {
            return zaclr_result_ok();
        }

        data = (const uint8_t*)zaclr_array_data_const(array);
        element_size = zaclr_array_element_size(array);
        if (data == NULL || element_size < sizeof(struct zaclr_object_desc*))
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        for (index = 0u; index < zaclr_array_length(array); ++index)
        {
            struct zaclr_object_desc* object = NULL;
            zaclr_object_handle handle = 0u;
            kernel_memcpy(&object, data + ((size_t)index * element_size), sizeof(object));
            if (object != NULL && frame.runtime != NULL)
            {
                handle = zaclr_heap_get_object_handle(&frame.runtime->heap, object);
            }

            if (index != 0u)
            {
                console_write(" ");
            }

            if (handle != 0u)
            {
                struct zaclr_stack_value temp = {};
                struct zaclr_stack_value* saved_arguments = frame.arguments;
                uint8_t saved_count = frame.argument_count;
                temp.kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
                temp.data.object_reference = object;
                frame.arguments = &temp;
                frame.argument_count = 1u;
                status = console_write_object_at(frame, 0u);
                frame.arguments = saved_arguments;
                frame.argument_count = saved_count;
                if (status.status != ZACLR_STATUS_OK)
                {
                    return status;
                }
            }
        }

        return zaclr_result_ok();
    }

    static struct zaclr_result console_write_formatted(struct zaclr_native_call_frame& frame,
                                                       uint32_t fixed_argument_count,
                                                       bool params_array,
                                                       bool append_newline)
    {
        struct zaclr_result status = console_write_string_arg(frame, 0u, false);
        uint32_t index;
        if (status.status != ZACLR_STATUS_OK)
        {
            return status;
        }

        if (params_array)
        {
            status = console_write_format_array(frame, 1u);
            if (status.status != ZACLR_STATUS_OK)
            {
                return status;
            }
        }
        else
        {
            for (index = 0u; index < fixed_argument_count; ++index)
            {
                status = console_write_format_object(frame, index + 1u);
                if (status.status != ZACLR_STATUS_OK)
                {
                    return status;
                }
            }
        }

        return console_set_void(frame, append_newline);
    }

    static struct zaclr_result console_write_span_placeholder(struct zaclr_native_call_frame& frame,
                                                              bool append_newline)
    {
        (void)frame;
        return console_set_void(frame, append_newline);
    }
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID(struct zaclr_native_call_frame& frame)
{
    return console_set_void(frame, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__BOOLEAN(struct zaclr_native_call_frame& frame)
{
    return console_write_bool_arg(frame, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__CHAR(struct zaclr_native_call_frame& frame)
{
    return console_write_char_arg(frame, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__SZARRAY_CHAR(struct zaclr_native_call_frame& frame)
{
    return console_write_char_array_range(frame, false, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__SZARRAY_CHAR__I4__I4(struct zaclr_native_call_frame& frame)
{
    return console_write_char_array_range(frame, true, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__VALUETYPE_System_Decimal(struct zaclr_native_call_frame& frame)
{
    return console_write_decimal_arg(frame, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__R8(struct zaclr_native_call_frame& frame)
{
    return console_write_r8_arg(frame, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__R4(struct zaclr_native_call_frame& frame)
{
    return console_write_r4_arg(frame, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__I4(struct zaclr_native_call_frame& frame)
{
    return console_write_i4_arg(frame, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__U4(struct zaclr_native_call_frame& frame)
{
    return console_write_u4_arg(frame, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__I8(struct zaclr_native_call_frame& frame)
{
    return console_write_i8_arg(frame, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__U8(struct zaclr_native_call_frame& frame)
{
    return console_write_u8_arg(frame, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame)
{
    return console_write_object_arg(frame, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__STRING(struct zaclr_native_call_frame& frame)
{
    return console_write_string_arg(frame, 0u, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__GENERICINST_VALUETYPE_System_ReadOnlySpanG1__1__CHAR(struct zaclr_native_call_frame& frame)
{
    return console_write_span_placeholder(frame, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__STRING__OBJECT(struct zaclr_native_call_frame& frame)
{
    return console_write_formatted(frame, 1u, false, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__STRING__OBJECT__OBJECT(struct zaclr_native_call_frame& frame)
{
    return console_write_formatted(frame, 2u, false, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__STRING__OBJECT__OBJECT__OBJECT(struct zaclr_native_call_frame& frame)
{
    return console_write_formatted(frame, 3u, false, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__STRING__SZARRAY_OBJECT(struct zaclr_native_call_frame& frame)
{
    return console_write_formatted(frame, 0u, true, true);
}

struct zaclr_result zaclr_native_System_Console::WriteLine___STATIC__VOID__STRING__GENERICINST_VALUETYPE_System_ReadOnlySpanG1__1__OBJECT(struct zaclr_native_call_frame& frame)
{
    return console_write_formatted(frame, 0u, false, true);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__STRING__OBJECT(struct zaclr_native_call_frame& frame)
{
    return console_write_formatted(frame, 1u, false, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__STRING__OBJECT__OBJECT(struct zaclr_native_call_frame& frame)
{
    return console_write_formatted(frame, 2u, false, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__STRING__OBJECT__OBJECT__OBJECT(struct zaclr_native_call_frame& frame)
{
    return console_write_formatted(frame, 3u, false, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__STRING__SZARRAY_OBJECT(struct zaclr_native_call_frame& frame)
{
    return console_write_formatted(frame, 0u, true, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__STRING__GENERICINST_VALUETYPE_System_ReadOnlySpanG1__1__OBJECT(struct zaclr_native_call_frame& frame)
{
    return console_write_formatted(frame, 0u, false, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__BOOLEAN(struct zaclr_native_call_frame& frame)
{
    return console_write_bool_arg(frame, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__CHAR(struct zaclr_native_call_frame& frame)
{
    return console_write_char_arg(frame, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__SZARRAY_CHAR(struct zaclr_native_call_frame& frame)
{
    return console_write_char_array_range(frame, false, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__SZARRAY_CHAR__I4__I4(struct zaclr_native_call_frame& frame)
{
    return console_write_char_array_range(frame, true, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__R8(struct zaclr_native_call_frame& frame)
{
    return console_write_r8_arg(frame, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__VALUETYPE_System_Decimal(struct zaclr_native_call_frame& frame)
{
    return console_write_decimal_arg(frame, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__R4(struct zaclr_native_call_frame& frame)
{
    return console_write_r4_arg(frame, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__I4(struct zaclr_native_call_frame& frame)
{
    return console_write_i4_arg(frame, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__U4(struct zaclr_native_call_frame& frame)
{
    return console_write_u4_arg(frame, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__I8(struct zaclr_native_call_frame& frame)
{
    return console_write_i8_arg(frame, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__U8(struct zaclr_native_call_frame& frame)
{
    return console_write_u8_arg(frame, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame)
{
    return console_write_object_arg(frame, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__STRING(struct zaclr_native_call_frame& frame)
{
    return console_write_string_arg(frame, 0u, false);
}

struct zaclr_result zaclr_native_System_Console::Write___STATIC__VOID__GENERICINST_VALUETYPE_System_ReadOnlySpanG1__1__CHAR(struct zaclr_native_call_frame& frame)
{
    return console_write_span_placeholder(frame, false);
}
