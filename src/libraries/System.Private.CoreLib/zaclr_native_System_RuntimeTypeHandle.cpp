#include "zaclr_native_System_RuntimeTypeHandle.h"

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/metadata/zaclr_type_map.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_method_table.h>
#include <kernel/zaclr/typesystem/zaclr_type_identity.h>
#include <kernel/zaclr/typesystem/zaclr_type_prepare.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static bool text_equals(const char* left, const char* right)
    {
        size_t index = 0u;

        if (left == NULL || right == NULL)
        {
            return left == right;
        }

        while (left[index] != '\0' || right[index] != '\0')
        {
            if (left[index] != right[index])
            {
                return false;
            }

            ++index;
        }

        return true;
    }

    static struct zaclr_result load_runtime_type_handle_argument(struct zaclr_native_call_frame& frame,
                                                                 uint32_t index,
                                                                 const struct zaclr_runtime_type_desc** out_type,
                                                                 zaclr_object_handle* out_handle)
    {
        struct zaclr_type_identity identity = {};
        struct zaclr_result result;

        if (out_type == NULL || out_handle == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_type = NULL;
        *out_handle = 0u;

        result = zaclr_native_call_frame_arg_object(&frame, index, out_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (*out_handle == 0u)
        {
            return zaclr_result_ok();
        }

        result = zaclr_type_identity_from_runtime_type_handle(frame.runtime, *out_handle, &identity);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
        zaclr_type_identity_reset(&identity);

        *out_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, *out_handle);
        return *out_type != NULL
            ? zaclr_result_ok()
            : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    static struct zaclr_result load_runtime_type_from_i8(struct zaclr_native_call_frame& frame,
                                                         uint32_t index,
                                                         const struct zaclr_runtime_type_desc** out_type,
                                                         zaclr_object_handle* out_handle)
    {
        int64_t raw_handle = 0;
        struct zaclr_result result;

        if (out_type == NULL || out_handle == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_type = NULL;
        *out_handle = 0u;

        result = zaclr_native_call_frame_arg_i8(&frame, index, &raw_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (raw_handle == 0)
        {
            return zaclr_result_ok();
        }

        result = zaclr_runtime_type_find_by_native_handle(frame.runtime,
                                                          (uintptr_t)raw_handle,
                                                          out_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, *out_handle);
        return *out_type != NULL
            ? zaclr_result_ok()
            : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    static struct zaclr_result load_qcall_type_handle_argument(struct zaclr_native_call_frame& frame,
                                                               uint32_t index,
                                                               const struct zaclr_runtime_type_desc** out_type,
                                                               zaclr_object_handle* out_handle)
    {
        struct zaclr_stack_value* argument;
        const uint8_t* payload;
        uint64_t referenced_slot_raw = 0u;
        uint64_t native_handle_raw;
        struct zaclr_result result;

        if (out_type == NULL || out_handle == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_type = NULL;
        *out_handle = 0u;

        argument = zaclr_native_call_frame_arg(&frame, index);
        if (argument == NULL || argument->kind != ZACLR_STACK_VALUE_VALUETYPE || argument->payload_size < 16u)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        payload = (const uint8_t*)zaclr_stack_value_payload_const(argument);
        if (payload == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        kernel_memcpy(&referenced_slot_raw, payload, sizeof(referenced_slot_raw));
        kernel_memcpy(&native_handle_raw, payload + sizeof(uint64_t), sizeof(native_handle_raw));
        console_write("[ZACLR][rtth-qcall] ptr=");
        console_write_hex64((uint64_t)referenced_slot_raw);
        console_write(" handle=");
        console_write_hex64((uint64_t)native_handle_raw);
        console_write(" arg_kind=");
        console_write_dec((uint64_t)argument->kind);
        console_write(" payload=");
        console_write_dec((uint64_t)argument->payload_size);
        console_write("\n");
        if (native_handle_raw == 0u)
        {
            return zaclr_result_ok();
        }

        result = zaclr_runtime_type_find_by_native_handle(frame.runtime,
                                                          (uintptr_t)native_handle_raw,
                                                          out_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            const struct zaclr_object_desc* direct_object = (const struct zaclr_object_desc*)(uintptr_t)native_handle_raw;
            console_write("[ZACLR][rtth-qcall] lookup miss status=");
            console_write_dec((uint64_t)result.status);
            console_write(" category=");
            console_write_dec((uint64_t)result.category);
            console_write(" direct_family=");
            console_write_dec((uint64_t)(direct_object != NULL ? direct_object->family : 0u));
            console_write(" direct_size=");
            console_write_dec((uint64_t)(direct_object != NULL ? direct_object->size_bytes : 0u));
            console_write("\n");
            if (direct_object != NULL && direct_object->family == ZACLR_OBJECT_FAMILY_RUNTIME_TYPE)
            {
                *out_handle = (zaclr_object_handle)(uintptr_t)direct_object;
                *out_type = (const struct zaclr_runtime_type_desc*)direct_object;
                return zaclr_result_ok();
            }

            if (referenced_slot_raw != 0u)
            {
                struct zaclr_stack_value* referenced_slot = (struct zaclr_stack_value*)(uintptr_t)referenced_slot_raw;
                console_write("[ZACLR][rtth-qcall] ref_slot kind=");
                console_write_dec((uint64_t)referenced_slot->kind);
                console_write(" flags=");
                console_write_hex64((uint64_t)referenced_slot->flags);
                console_write(" payload=");
                console_write_dec((uint64_t)referenced_slot->payload_size);
                console_write(" raw=");
                console_write_hex64((uint64_t)referenced_slot->data.raw);
                console_write("\n");
                if (referenced_slot->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
                {
                    zaclr_object_handle referenced_handle = zaclr_heap_get_object_handle(&frame.runtime->heap,
                                                                                         referenced_slot->data.object_reference);
                    const struct zaclr_runtime_type_desc* referenced_type = referenced_handle != 0u
                        ? zaclr_runtime_type_from_handle_const(&frame.runtime->heap, referenced_handle)
                        : NULL;
                    if (referenced_type != NULL)
                    {
                        *out_handle = referenced_handle;
                        *out_type = referenced_type;
                        return zaclr_result_ok();
                    }
                }

                if (referenced_slot->kind == ZACLR_STACK_VALUE_VALUETYPE && referenced_slot->payload_size >= sizeof(uintptr_t))
                {
                    const uint8_t* referenced_payload = (const uint8_t*)zaclr_stack_value_payload_const(referenced_slot);
                    uintptr_t referenced_native_handle = 0u;
                    if (referenced_payload != NULL)
                    {
                        kernel_memcpy(&referenced_native_handle, referenced_payload, sizeof(referenced_native_handle));
                        if (referenced_native_handle != 0u)
                        {
                            result = zaclr_runtime_type_find_by_native_handle(frame.runtime,
                                                                              referenced_native_handle,
                                                                              out_handle);
                            if (result.status == ZACLR_STATUS_OK && *out_handle != 0u)
                            {
                                *out_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, *out_handle);
                                return *out_type != NULL
                                    ? zaclr_result_ok()
                                    : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
                            }
                        }
                    }
                }
            }

            return result;
        }

        *out_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, *out_handle);
        return *out_type != NULL
            ? zaclr_result_ok()
            : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    static int32_t cor_element_type_for_runtime_type(const struct zaclr_runtime_type_desc* runtime_type)
    {
        if (runtime_type == NULL)
        {
            return ZACLR_ELEMENT_TYPE_END;
        }

        if (runtime_type->type_assembly != NULL
            && zaclr_token_matches_table(&runtime_type->type_token, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            const struct zaclr_type_desc* type_desc = zaclr_type_map_find_by_token(&runtime_type->type_assembly->type_map,
                                                                                   runtime_type->type_token);
            if (type_desc != NULL)
            {
                if (type_desc->type_namespace.text != NULL && type_desc->type_name.text != NULL)
                {
                    if (text_equals(type_desc->type_namespace.text, "System"))
                    {
                        if (text_equals(type_desc->type_name.text, "Boolean")) return ZACLR_ELEMENT_TYPE_BOOLEAN;
                        if (text_equals(type_desc->type_name.text, "Char")) return ZACLR_ELEMENT_TYPE_CHAR;
                        if (text_equals(type_desc->type_name.text, "SByte")) return ZACLR_ELEMENT_TYPE_I1;
                        if (text_equals(type_desc->type_name.text, "Byte")) return ZACLR_ELEMENT_TYPE_U1;
                        if (text_equals(type_desc->type_name.text, "Int16")) return ZACLR_ELEMENT_TYPE_I2;
                        if (text_equals(type_desc->type_name.text, "UInt16")) return ZACLR_ELEMENT_TYPE_U2;
                        if (text_equals(type_desc->type_name.text, "Int32")) return ZACLR_ELEMENT_TYPE_I4;
                        if (text_equals(type_desc->type_name.text, "UInt32")) return ZACLR_ELEMENT_TYPE_U4;
                        if (text_equals(type_desc->type_name.text, "Int64")) return ZACLR_ELEMENT_TYPE_I8;
                        if (text_equals(type_desc->type_name.text, "UInt64")) return ZACLR_ELEMENT_TYPE_U8;
                        if (text_equals(type_desc->type_name.text, "Single")) return ZACLR_ELEMENT_TYPE_R4;
                        if (text_equals(type_desc->type_name.text, "Double")) return ZACLR_ELEMENT_TYPE_R8;
                        if (text_equals(type_desc->type_name.text, "String")) return ZACLR_ELEMENT_TYPE_STRING;
                        if (text_equals(type_desc->type_name.text, "IntPtr")) return ZACLR_ELEMENT_TYPE_I;
                        if (text_equals(type_desc->type_name.text, "UIntPtr")) return ZACLR_ELEMENT_TYPE_U;
                        if (text_equals(type_desc->type_name.text, "Object")) return ZACLR_ELEMENT_TYPE_OBJECT;
                    }
                }
            }
        }

        return ZACLR_ELEMENT_TYPE_CLASS;
    }

    static struct zaclr_result get_or_create_runtime_type_native_handle(struct zaclr_native_call_frame& frame,
                                                                        const struct zaclr_loaded_assembly* assembly,
                                                                        struct zaclr_token type_token,
                                                                        uintptr_t* out_native_handle)
    {
        uint32_t type_row;
        zaclr_object_handle runtime_type_handle;
        struct zaclr_runtime_type_desc* runtime_type;
        const struct zaclr_runtime_type_desc* runtime_type_const;
        struct zaclr_result result;

        if (frame.runtime == NULL || assembly == NULL || out_native_handle == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_native_handle = 0u;
        if (!zaclr_token_matches_table(&type_token, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        type_row = zaclr_token_row(&type_token);
        if (type_row == 0u || type_row > assembly->runtime_type_cache_count || assembly->runtime_type_cache == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        runtime_type_handle = assembly->runtime_type_cache[type_row - 1u];
        if (runtime_type_handle == 0u)
        {
            result = zaclr_runtime_type_allocate(&frame.runtime->heap,
                                                assembly,
                                                type_token,
                                                &runtime_type);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            runtime_type_handle = zaclr_heap_get_object_handle(&frame.runtime->heap, &runtime_type->object);
            ((struct zaclr_loaded_assembly*)assembly)->runtime_type_cache[type_row - 1u] = runtime_type_handle;
        }

        runtime_type_const = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, runtime_type_handle);
        if (runtime_type_const == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_native_handle = runtime_type_const->native_type_handle;
        return zaclr_result_ok();
    }

    static const struct zaclr_type_desc* runtime_type_get_type_desc(const struct zaclr_runtime_type_desc* runtime_type)
    {
        if (runtime_type == NULL
            || runtime_type->type_assembly == NULL
            || !zaclr_token_matches_table(&runtime_type->type_token, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            return NULL;
        }

        return zaclr_type_map_find_by_token(&runtime_type->type_assembly->type_map, runtime_type->type_token);
    }

    static struct zaclr_result runtime_type_prepare_method_table(struct zaclr_native_call_frame& frame,
                                                                 const struct zaclr_runtime_type_desc* runtime_type,
                                                                 struct zaclr_method_table** out_method_table)
    {
        const struct zaclr_type_desc* type_desc;

        if (out_method_table == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_method_table = NULL;
        type_desc = runtime_type_get_type_desc(runtime_type);
        if (frame.runtime == NULL || runtime_type == NULL || runtime_type->type_assembly == NULL || type_desc == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        return zaclr_type_prepare(frame.runtime,
                                  (struct zaclr_loaded_assembly*)runtime_type->type_assembly,
                                  type_desc,
                                  out_method_table);
    }

    static struct zaclr_result set_runtime_method_handle_internal(struct zaclr_native_call_frame& frame,
                                                                  uintptr_t method_handle)
    {
        struct zaclr_stack_value value = {};
        uint8_t bytes[sizeof(uintptr_t)] = {};
        kernel_memcpy(bytes, &method_handle, sizeof(method_handle));
        struct zaclr_result result = zaclr_stack_value_set_valuetype(&value,
                                                                     ((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | 0x6Bu,
                                                                     bytes,
                                                                     (uint32_t)sizeof(bytes));
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        frame.has_result = 1u;
        return zaclr_stack_value_assign(&frame.result_value, &value);
    }

    static struct zaclr_result store_byref_runtime_method_handle_internal(struct zaclr_native_call_frame& frame,
                                                                          uint32_t index,
                                                                          uintptr_t method_handle)
    {
        struct zaclr_stack_value* argument = zaclr_native_call_frame_arg(&frame, index);
        uint8_t bytes[sizeof(uintptr_t)] = {};
        kernel_memcpy(bytes, &method_handle, sizeof(method_handle));

        if (argument == NULL || argument->kind != ZACLR_STACK_VALUE_BYREF)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if ((argument->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) != 0u)
        {
            struct zaclr_stack_value* target = (struct zaclr_stack_value*)(uintptr_t)argument->data.raw;
            return target != NULL
                ? zaclr_stack_value_set_valuetype(target,
                                                  ((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | 0x6Bu,
                                                  bytes,
                                                  (uint32_t)sizeof(bytes))
                : zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if (argument->data.raw == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        kernel_memcpy((void*)(uintptr_t)argument->data.raw, bytes, sizeof(bytes));
        return zaclr_result_ok();
    }

    static const char* runtime_type_name_or_fallback(const struct zaclr_runtime_type_desc* runtime_type)
    {
        const struct zaclr_type_desc* type_desc = runtime_type_get_type_desc(runtime_type);
        if (type_desc == NULL || type_desc->type_name.text == NULL)
        {
            return "Object";
        }

        return type_desc->type_name.text;
    }

    static const char* runtime_type_namespace_or_empty(const struct zaclr_runtime_type_desc* runtime_type)
    {
        const struct zaclr_type_desc* type_desc = runtime_type_get_type_desc(runtime_type);
        return type_desc != NULL && type_desc->type_namespace.text != NULL
            ? type_desc->type_namespace.text
            : "";
    }

    static uint32_t text_length(const char* text)
    {
        uint32_t length = 0u;
        if (text == NULL)
        {
            return 0u;
        }

        while (text[length] != '\0')
        {
            ++length;
        }

        return length;
    }

    static struct zaclr_result allocate_runtime_type_name(struct zaclr_native_call_frame& frame,
                                                          const struct zaclr_runtime_type_desc* runtime_type,
                                                          int32_t format_flags,
                                                          zaclr_object_handle* out_handle)
    {
        const char* type_namespace = runtime_type_namespace_or_empty(runtime_type);
        const char* type_name = runtime_type_name_or_fallback(runtime_type);
        const uint32_t namespace_length = text_length(type_namespace);
        const uint32_t name_length = text_length(type_name);
        const bool include_namespace = (format_flags & 0x00000002) != 0 && namespace_length != 0u;
        const uint32_t total_length = name_length + (include_namespace ? namespace_length + 1u : 0u);
        char inline_buffer[192] = {};
        char* name_buffer = inline_buffer;
        struct zaclr_result result;
        uint32_t cursor = 0u;

        if (out_handle == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }
        *out_handle = 0u;

        if (frame.runtime == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if (total_length + 1u > sizeof(inline_buffer))
        {
            name_buffer = (char*)kernel_alloc(total_length + 1u);
            if (name_buffer == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_HEAP);
            }
        }

        if (include_namespace)
        {
            for (uint32_t index = 0u; index < namespace_length; ++index)
            {
                name_buffer[cursor++] = type_namespace[index];
            }
            name_buffer[cursor++] = '.';
        }

        for (uint32_t index = 0u; index < name_length; ++index)
        {
            name_buffer[cursor++] = type_name[index];
        }
        name_buffer[cursor] = '\0';

        result = zaclr_string_allocate_ascii_handle(&frame.runtime->heap,
                                                    name_buffer,
                                                    total_length,
                                                    out_handle);
        if (name_buffer != inline_buffer)
        {
            kernel_free(name_buffer);
        }

        return result;
    }

    static struct zaclr_result store_string_handle_on_stack(struct zaclr_native_call_frame& frame,
                                                            uint32_t index,
                                                            zaclr_object_handle string_handle)
    {
        struct zaclr_stack_value* argument = zaclr_native_call_frame_arg(&frame, index);
        const uint8_t* payload;
        uintptr_t target_raw = 0u;
        struct zaclr_stack_value* target;

        if (argument == NULL
            || argument->kind != ZACLR_STACK_VALUE_VALUETYPE
            || argument->payload_size < sizeof(target_raw))
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        payload = (const uint8_t*)zaclr_stack_value_payload_const(argument);
        if (payload == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        kernel_memcpy(&target_raw, payload, sizeof(target_raw));
        console_write("[ZACLR][rtth-string-on-stack] target=");
        console_write_hex64((uint64_t)target_raw);
        console_write(" arg_kind=");
        console_write_dec((uint64_t)argument->kind);
        console_write(" arg_payload=");
        console_write_dec((uint64_t)argument->payload_size);
        console_write(" string_handle=");
        console_write_hex64((uint64_t)string_handle);
        console_write("\n");
        if (target_raw == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        target = (struct zaclr_stack_value*)target_raw;
        console_write("[ZACLR][rtth-string-on-stack] slot_kind=");
        console_write_dec((uint64_t)target->kind);
        console_write(" slot_flags=");
        console_write_hex64((uint64_t)target->flags);
        console_write(" slot_payload=");
        console_write_dec((uint64_t)target->payload_size);
        console_write(" slot_raw=");
        console_write_hex64((uint64_t)target->data.raw);
        console_write("\n");
        target->kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        target->reserved = 0u;
        target->payload_size = sizeof(uintptr_t);
        target->type_token_raw = 0u;
        target->flags = ZACLR_STACK_VALUE_FLAG_NONE;
        target->extra = 0u;
        target->data.object_reference = frame.runtime != NULL
            ? zaclr_heap_get_object(&frame.runtime->heap, string_handle)
            : NULL;
        return zaclr_result_ok();
    }
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::RuntimeTypeHandle_GetRuntimeTypeFromHandleSlow___STATIC__VOID__I__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_from_i8(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_store_byref_object(&frame, 1u, runtime_type_handle);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::ToIntPtr___STATIC__I__VALUETYPE(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_i8(&frame,
                                          runtime_type != NULL
                                              ? (int64_t)runtime_type->native_type_handle
                                              : 0);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::FromIntPtr___STATIC__VALUETYPE__I(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_from_i8(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_object(&frame, runtime_type_handle);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::InternalAllocNoChecks_FastPath___STATIC__OBJECT__PTR_VALUETYPE_System_Runtime_CompilerServices_MethodTable(struct zaclr_native_call_frame& frame)
{
    /* Returning null preserves CoreLib's slow QCall fallback path without claiming
       a fast allocator ZACLR does not yet implement. */
    return zaclr_native_call_frame_set_object(&frame, 0u);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetToken___STATIC__I4__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (runtime_type == NULL || !zaclr_token_matches_table(&runtime_type->type_token, ZACLR_TOKEN_TABLE_TYPEDEF))
    {
        return zaclr_native_call_frame_set_i4(&frame, (int32_t)((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24));
    }

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)runtime_type->type_token.raw);
}

/* CoreCLR: RuntimeTypeHandle.GetAssemblyIfExists(RuntimeType type) -> RuntimeAssembly?
   FCALL (InternalCall), static, takes RuntimeType object, returns RuntimeAssembly or null.
   Reference: CLONES/runtime/src/coreclr/vm/runtimehandles.cpp:220-231
   
   In CoreCLR this extracts the MethodTable from the RuntimeType, gets the
   Assembly* from the MethodTable's Module, then returns the Assembly's
   managed "exposed object" (RuntimeAssembly) if it has already been created.
   
   In ZACLR, the RuntimeType stores its type_assembly directly.  We lazily
   create the managed RuntimeAssembly via zaclr_runtime_assembly_get_or_create. */
struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetAssemblyIfExists___STATIC__CLASS_System_Reflection_RuntimeAssembly__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    zaclr_object_handle assembly_handle = 0u;
    struct zaclr_result result;

    /* Argument 0: RuntimeType type */
    result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (runtime_type == NULL || runtime_type->type_assembly == NULL)
    {
        /* No type or no assembly -> return null (caller falls back to GetAssemblySlow) */
        return zaclr_native_call_frame_set_object(&frame, 0u);
    }

    /* Get or create the managed RuntimeAssembly for this assembly */
    result = zaclr_runtime_assembly_get_or_create(&frame.runtime->heap,
                                                   (struct zaclr_loaded_assembly*)runtime_type->type_assembly,
                                                   &assembly_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        /* If allocation fails, return null to let caller try the slow path */
        return zaclr_native_call_frame_set_object(&frame, 0u);
    }

    return zaclr_native_call_frame_set_object(&frame, assembly_handle);
}

/* CoreCLR: RuntimeTypeHandle.GetModuleIfExists(RuntimeType type) -> RuntimeModule?
   The immediate caller falls back to QCall GetModuleSlow if this returns null.
   ZACLR currently has no managed RuntimeModule object surface, but GetModule() only
   needs a module whose RuntimeModule.get_RuntimeType() returns the declaring type's
   RuntimeType. Returning the existing RuntimeAssembly object preserves forward motion
   for the current interpreter slice because both RuntimeAssembly and RuntimeModule are
   managed wrappers around the owning native assembly/module concept. */
struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetModuleIfExists___STATIC__CLASS_System_Reflection_RuntimeModule__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    zaclr_object_handle assembly_handle = 0u;
    struct zaclr_result result;

    console_write("[ZACLR][rtth] compiled GetModuleIfExists reached\n");

    result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (runtime_type == NULL || runtime_type->type_assembly == NULL)
    {
        return zaclr_native_call_frame_set_object(&frame, 0u);
    }

    result = zaclr_runtime_assembly_get_or_create(&frame.runtime->heap,
                                                   (struct zaclr_loaded_assembly*)runtime_type->type_assembly,
                                                   &assembly_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_object(&frame, 0u);
    }

    return zaclr_native_call_frame_set_object(&frame, assembly_handle);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetAttributes___STATIC__VALUETYPE_System_Reflection_TypeAttributes__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    const struct zaclr_type_desc* type_desc;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    type_desc = runtime_type_get_type_desc(runtime_type);
    return zaclr_native_call_frame_set_i4(&frame, type_desc != NULL ? (int32_t)type_desc->flags : 0);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetElementTypeHandle___STATIC__I__I(struct zaclr_native_call_frame& frame)
{
    /* ZACLR has no element-type runtime descriptors yet for array, pointer, or byref
       TypeHandles. Return zero for ordinary type definitions so CoreLib sees no element type. */
    int64_t raw_handle = 0;
    struct zaclr_result result = zaclr_native_call_frame_arg_i8(&frame, 0u, &raw_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)raw_handle;
    return zaclr_native_call_frame_set_i8(&frame, 0);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::CompareCanonicalHandles___STATIC__BOOLEAN__CLASS_System_RuntimeType__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle left_handle = 0u;
    zaclr_object_handle right_handle = 0u;
    struct zaclr_result result = zaclr_native_call_frame_arg_object(&frame, 0u, &left_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_native_call_frame_arg_object(&frame, 1u, &right_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_bool(&frame, left_handle == right_handle);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetArrayRank___STATIC__I4__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)runtime_type;
    return zaclr_native_call_frame_set_i4(&frame, 0);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::IsUnmanagedFunctionPointer___STATIC__BOOLEAN__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)runtime_type;
    return zaclr_native_call_frame_set_bool(&frame, false);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetFirstIntroducedMethod___STATIC__VALUETYPE_System_RuntimeMethodHandleInternal__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)runtime_type;
    return set_runtime_method_handle_internal(frame, 0u);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetNextIntroducedMethod___STATIC__VOID__BYREF_VALUETYPE_System_RuntimeMethodHandleInternal(struct zaclr_native_call_frame& frame)
{
    struct zaclr_result result = store_byref_runtime_method_handle_internal(frame, 0u, 0u);
    return result.status == ZACLR_STATUS_OK ? zaclr_native_call_frame_set_void(&frame) : result;
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetNumVirtuals___STATIC__I4__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_method_table* method_table = NULL;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = runtime_type_prepare_method_table(frame, runtime_type, &method_table);
    if (result.status != ZACLR_STATUS_OK || method_table == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, 0);
    }

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)method_table->vtable_slot_count);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetUtf8NameInternal___STATIC__PTR_U1__PTR_VALUETYPE_System_Runtime_CompilerServices_MethodTable(struct zaclr_native_call_frame& frame)
{
    int64_t raw_method_table = 0;
    const struct zaclr_method_table* method_table;
    struct zaclr_result result = zaclr_native_call_frame_arg_i8(&frame, 0u, &raw_method_table);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    method_table = (const struct zaclr_method_table*)(uintptr_t)raw_method_table;
    if (method_table == NULL || method_table->type_desc == NULL || method_table->type_desc->type_name.text == NULL)
    {
        return zaclr_native_call_frame_set_i8(&frame, 0);
    }

    return zaclr_native_call_frame_set_i8(&frame, (int64_t)(uintptr_t)method_table->type_desc->type_name.text);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::IsGenericVariable___STATIC__BOOLEAN__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)runtime_type;
    return zaclr_native_call_frame_set_bool(&frame, false);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetGenericVariableIndex___STATIC__I4__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)runtime_type;
    return zaclr_native_call_frame_set_i4(&frame, -1);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::ContainsGenericVariables___STATIC__BOOLEAN__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)runtime_type;
    return zaclr_native_call_frame_set_bool(&frame, false);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetGCHandle___I__VALUETYPE_System_Runtime_InteropServices_GCHandleType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    int32_t handle_kind = 0;
    uint32_t index = 0u;
    struct zaclr_handle_table* table;
    struct zaclr_result result;

    result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_native_call_frame_arg_i4(&frame, 1u, &handle_kind);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    table = frame.runtime != NULL ? &frame.runtime->boot_launch.handle_table : NULL;
    if (table == NULL || runtime_type_handle == 0u)
    {
        return zaclr_native_call_frame_set_i8(&frame, 0);
    }

    result = zaclr_handle_table_store_ex(table, 0u, (uint32_t)handle_kind, &index);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)runtime_type;
    return zaclr_native_call_frame_set_i8(&frame, (int64_t)(uintptr_t)&table->entries[index].handle);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::FreeGCHandle___I__I(struct zaclr_native_call_frame& frame)
{
    int64_t handle_value = 0;
    struct zaclr_result result = zaclr_native_call_frame_arg_i8(&frame, 0u, &handle_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_i8(&frame, handle_value);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::QCall_GetGCHandleForTypeHandle(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    int32_t handle_kind = 0;
    uint32_t index = 0u;
    struct zaclr_handle_table* table;
    struct zaclr_result result;

    result = load_qcall_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_native_call_frame_arg_i4(&frame, 1u, &handle_kind);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    table = frame.runtime != NULL ? &frame.runtime->boot_launch.handle_table : NULL;
    if (table == NULL || runtime_type_handle == 0u)
    {
        return zaclr_native_call_frame_set_i8(&frame, 0);
    }

    result = zaclr_handle_table_store_ex(table, 0u, (uint32_t)handle_kind, &index);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)runtime_type;
    return zaclr_native_call_frame_set_i8(&frame, (int64_t)(uintptr_t)&table->entries[index].handle);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::QCall_FreeGCHandleForTypeHandle(struct zaclr_native_call_frame& frame)
{
    int64_t handle_value = 0;
    struct zaclr_result result = zaclr_native_call_frame_arg_i8(&frame, 1u, &handle_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_i8(&frame, handle_value);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::QCall_TypeHandle_GetCorElementType(struct zaclr_native_call_frame& frame)
{
    int64_t raw_type_handle = 0;
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result;

    result = zaclr_native_call_frame_arg_i8(&frame, 0u, &raw_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (raw_type_handle == 0)
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_ELEMENT_TYPE_END);
    }

    result = zaclr_runtime_type_find_by_native_handle(frame.runtime,
                                                      (uintptr_t)raw_type_handle,
                                                      &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    runtime_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, runtime_type_handle);
    return zaclr_native_call_frame_set_i4(&frame, cor_element_type_for_runtime_type(runtime_type));
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::QCall_RuntimeTypeHandle_GetDeclaringTypeHandle(struct zaclr_native_call_frame& frame)
{
    int64_t raw_type_handle = 0;
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    uint32_t nested_row_count;
    struct zaclr_result result;

    result = zaclr_native_call_frame_arg_i8(&frame, 0u, &raw_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (raw_type_handle == 0 || frame.runtime == NULL)
    {
        return zaclr_native_call_frame_set_i8(&frame, 0);
    }

    result = zaclr_runtime_type_find_by_native_handle(frame.runtime,
                                                      (uintptr_t)raw_type_handle,
                                                      &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_i8(&frame, 0);
    }

    runtime_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, runtime_type_handle);
    if (runtime_type == NULL
        || runtime_type->type_assembly == NULL
        || !zaclr_token_matches_table(&runtime_type->type_token, ZACLR_TOKEN_TABLE_TYPEDEF))
    {
        return zaclr_native_call_frame_set_i8(&frame, 0);
    }

    nested_row_count = zaclr_metadata_reader_get_row_count(&runtime_type->type_assembly->metadata, 0x29u);
    for (uint32_t row_index = 1u; row_index <= nested_row_count; ++row_index)
    {
        struct zaclr_nestedclass_row nested_row = {};
        result = zaclr_metadata_reader_get_nestedclass_row(&runtime_type->type_assembly->metadata,
                                                           row_index,
                                                           &nested_row);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (nested_row.nested_class == zaclr_token_row(&runtime_type->type_token))
        {
            uintptr_t declaring_native_handle = 0u;
            struct zaclr_token declaring_token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | nested_row.enclosing_class);
            result = get_or_create_runtime_type_native_handle(frame,
                                                              runtime_type->type_assembly,
                                                              declaring_token,
                                                              &declaring_native_handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            return zaclr_native_call_frame_set_i8(&frame, (int64_t)declaring_native_handle);
        }
    }

    return zaclr_native_call_frame_set_i8(&frame, 0);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::QCall_RuntimeTypeHandle_ConstructName(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    zaclr_object_handle runtime_type_handle = 0u;
    zaclr_object_handle string_handle = 0u;
    int32_t format_flags = 0;
    struct zaclr_result result;

    result = load_qcall_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_native_call_frame_arg_i4(&frame, 1u, &format_flags);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = allocate_runtime_type_name(frame, runtime_type, format_flags, &string_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)runtime_type_handle;
    result = store_string_handle_on_stack(frame, 2u, string_handle);
    return result.status == ZACLR_STATUS_OK ? zaclr_native_call_frame_set_void(&frame) : result;
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::get_Module___CLASS_System_Reflection_Module(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle value = 0u;
    struct zaclr_result result = zaclr_native_call_frame_arg_object(&frame, 0u, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_object(&frame, value);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::get_ModuleHandle___VALUETYPE_System_ModuleHandle(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i8(&frame, 0);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::Equals___BOOLEAN__VALUETYPE_System_ModuleHandle(struct zaclr_native_call_frame& frame)
{
    (void)frame;
    return zaclr_native_call_frame_set_bool(&frame, true);
}
