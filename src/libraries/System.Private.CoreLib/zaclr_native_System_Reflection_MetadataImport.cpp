#include "zaclr_native_System_Reflection_MetadataImport.h"

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/loader/zaclr_assembly_registry.h>
#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_type_prepare.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

namespace
{
    enum metadata_import_result {
        ZACLR_METADATA_IMPORT_S_OK = 0,
        ZACLR_METADATA_IMPORT_E_FAIL = -1
    };

    struct zaclr_const_array_native {
        int32_t length;
        uintptr_t pointer;
    };

    static struct zaclr_result get_field_token(const struct zaclr_loaded_assembly* assembly,
                                               const char* type_namespace,
                                               const char* type_name,
                                               const char* field_name,
                                               struct zaclr_token* out_token)
    {
        struct zaclr_member_name_ref target_type = { type_namespace, type_name, NULL };
        const struct zaclr_type_desc* type_desc;

        if (assembly == NULL || field_name == NULL || out_token == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_token = zaclr_token_make(0u);
        type_desc = zaclr_type_system_find_type_by_name(assembly, &target_type);
        if (type_desc == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        for (uint32_t index = 0u; index < type_desc->field_count; ++index)
        {
            const uint32_t field_row = type_desc->field_list + index;
            struct zaclr_field_row row = {};
            struct zaclr_name_view name = {};
            struct zaclr_result result = zaclr_metadata_reader_get_field_row(&assembly->metadata, field_row, &row);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_metadata_reader_get_string(&assembly->metadata, row.name_index, &name);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (zaclr_text_equals(name.text, field_name))
            {
                *out_token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | field_row);
                return zaclr_result_ok();
            }
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    static struct zaclr_result load_intptr_field(struct zaclr_runtime* runtime,
                                                 zaclr_object_handle object_handle,
                                                 struct zaclr_token field_token,
                                                 uintptr_t* out_value)
    {
        struct zaclr_stack_value value = {};
        struct zaclr_result result;

        if (runtime == NULL || out_value == NULL || object_handle == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_value = 0u;
        result = zaclr_object_load_field_handle(runtime, object_handle, field_token, &value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (value.kind == ZACLR_STACK_VALUE_I8)
        {
            *out_value = (uintptr_t)value.data.i8;
            return zaclr_result_ok();
        }

        if (value.kind == ZACLR_STACK_VALUE_I4)
        {
            *out_value = (uintptr_t)(uint32_t)value.data.i4;
            return zaclr_result_ok();
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    static const struct zaclr_loaded_assembly* find_corelib(struct zaclr_runtime* runtime)
    {
        const struct zaclr_app_domain* domain = runtime != NULL ? zaclr_runtime_current_domain(runtime) : NULL;
        return domain != NULL
            ? zaclr_assembly_registry_find_by_name(&domain->registry, "System.Private.CoreLib")
            : NULL;
    }

    static struct zaclr_result load_metadata_reader_argument(struct zaclr_native_call_frame& frame,
                                                             uint32_t index,
                                                             const struct zaclr_metadata_reader** out_reader)
    {
        int64_t raw_reader = 0;
        struct zaclr_result result;

        if (out_reader == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_reader = NULL;
        result = zaclr_native_call_frame_arg_i8(&frame, index, &raw_reader);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_reader = (const struct zaclr_metadata_reader*)(uintptr_t)raw_reader;
        return zaclr_result_ok();
    }

    static struct zaclr_result store_byref_raw_pointer(struct zaclr_native_call_frame& frame,
                                                       uint32_t index,
                                                       const void* pointer)
    {
        struct zaclr_stack_value* argument = zaclr_native_call_frame_arg(&frame, index);
        uintptr_t value = (uintptr_t)pointer;

        if (argument == NULL || argument->kind != ZACLR_STACK_VALUE_BYREF)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if ((argument->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) != 0u)
        {
            struct zaclr_stack_value* target = (struct zaclr_stack_value*)(uintptr_t)argument->data.raw;
            if (target == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
            }

            target->kind = ZACLR_STACK_VALUE_I8;
            target->reserved = 0u;
            target->payload_size = sizeof(uintptr_t);
            target->type_token_raw = 0u;
            target->flags = ZACLR_STACK_VALUE_FLAG_NONE;
            target->extra = 0u;
            target->data.i8 = (int64_t)value;
            return zaclr_result_ok();
        }

        if (argument->data.raw == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        kernel_memcpy((void*)(uintptr_t)argument->data.raw, &value, sizeof(value));
        return zaclr_result_ok();
    }

    static struct zaclr_result store_byref_i4_raw(struct zaclr_native_call_frame& frame,
                                                  uint32_t index,
                                                  int32_t value)
    {
        struct zaclr_stack_value* argument = zaclr_native_call_frame_arg(&frame, index);

        if (argument == NULL || argument->kind != ZACLR_STACK_VALUE_BYREF)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if ((argument->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) != 0u)
        {
            struct zaclr_stack_value* target = (struct zaclr_stack_value*)(uintptr_t)argument->data.raw;
            if (target == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
            }

            target->kind = ZACLR_STACK_VALUE_I4;
            target->reserved = 0u;
            target->payload_size = sizeof(value);
            target->type_token_raw = 0u;
            target->flags = ZACLR_STACK_VALUE_FLAG_NONE;
            target->extra = 0u;
            target->data.i4 = value;
            return zaclr_result_ok();
        }

        if (argument->data.raw == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        kernel_memcpy((void*)(uintptr_t)argument->data.raw, &value, sizeof(value));
        return zaclr_result_ok();
    }

    static struct zaclr_result store_byref_i8_raw(struct zaclr_native_call_frame& frame,
                                                  uint32_t index,
                                                  int64_t value)
    {
        struct zaclr_stack_value* argument = zaclr_native_call_frame_arg(&frame, index);

        if (argument == NULL || argument->kind != ZACLR_STACK_VALUE_BYREF)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if ((argument->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) != 0u)
        {
            struct zaclr_stack_value* target = (struct zaclr_stack_value*)(uintptr_t)argument->data.raw;
            if (target == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
            }

            target->kind = ZACLR_STACK_VALUE_I8;
            target->reserved = 0u;
            target->payload_size = sizeof(value);
            target->type_token_raw = 0u;
            target->flags = ZACLR_STACK_VALUE_FLAG_NONE;
            target->extra = 0u;
            target->data.i8 = value;
            return zaclr_result_ok();
        }

        if (argument->data.raw == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        kernel_memcpy((void*)(uintptr_t)argument->data.raw, &value, sizeof(value));
        return zaclr_result_ok();
    }

    static struct zaclr_result store_byref_bool_raw(struct zaclr_native_call_frame& frame,
                                                    uint32_t index,
                                                    bool value)
    {
        return store_byref_i4_raw(frame, index, value ? 1 : 0);
    }

    static struct zaclr_result store_byref_const_array(struct zaclr_native_call_frame& frame,
                                                       uint32_t index,
                                                       const void* pointer,
                                                       uint32_t length)
    {
        struct zaclr_stack_value* argument = zaclr_native_call_frame_arg(&frame, index);
        struct zaclr_const_array_native value = { (int32_t)length, (uintptr_t)pointer };

        if (argument == NULL || argument->kind != ZACLR_STACK_VALUE_BYREF)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if ((argument->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) != 0u)
        {
            struct zaclr_stack_value* target = (struct zaclr_stack_value*)(uintptr_t)argument->data.raw;
            return target != NULL
                ? zaclr_stack_value_set_valuetype(target,
                                                  ((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | 0x76Au,
                                                  &value,
                                                  (uint32_t)sizeof(value))
                : zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if (argument->data.raw == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        kernel_memcpy((void*)(uintptr_t)argument->data.raw, &value, sizeof(value));
        return zaclr_result_ok();
    }

    static struct zaclr_result store_byref_guid_zero(struct zaclr_native_call_frame& frame,
                                                     uint32_t index)
    {
        struct zaclr_stack_value* argument = zaclr_native_call_frame_arg(&frame, index);
        uint8_t guid[16] = {};

        if (argument == NULL || argument->kind != ZACLR_STACK_VALUE_BYREF)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if ((argument->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) != 0u)
        {
            struct zaclr_stack_value* target = (struct zaclr_stack_value*)(uintptr_t)argument->data.raw;
            return target != NULL
                ? zaclr_stack_value_set_valuetype(target,
                                                  ((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | 0x43Cu,
                                                  guid,
                                                  (uint32_t)sizeof(guid))
                : zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if (argument->data.raw == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        kernel_memcpy((void*)(uintptr_t)argument->data.raw, guid, sizeof(guid));
        return zaclr_result_ok();
    }

    static struct zaclr_result load_scope_and_token(struct zaclr_native_call_frame& frame,
                                                    const struct zaclr_metadata_reader** out_reader,
                                                    struct zaclr_token* out_token)
    {
        int32_t raw_token = 0;
        struct zaclr_result result = load_metadata_reader_argument(frame, 0u, out_reader);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_native_call_frame_arg_i4(&frame, 1u, &raw_token);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (out_token != NULL)
        {
            *out_token = zaclr_token_make((uint32_t)raw_token);
        }
        return zaclr_result_ok();
    }

    static bool metadata_token_is_valid(const struct zaclr_metadata_reader* reader,
                                        struct zaclr_token token)
    {
        uint32_t table = zaclr_token_table(&token);
        uint32_t row = zaclr_token_row(&token);
        return reader != NULL
            && table < ZACLR_METADATA_MAX_TABLES
            && row != 0u
            && row <= zaclr_metadata_reader_get_row_count(reader, table);
    }

    static uint32_t methoddef_or_ref_coded_to_token(uint32_t coded)
    {
        uint32_t row = coded >> 1u;
        uint32_t table = (coded & 0x1u) == 0u ? ZACLR_TOKEN_TABLE_METHOD : ZACLR_TOKEN_TABLE_MEMBERREF;
        return (table << 24) | row;
    }

    static uint32_t has_constant_parent_to_token(uint32_t coded)
    {
        static const uint8_t tables[] = { 0x04u, 0x08u, 0x17u };
        uint32_t tag = coded & 0x3u;
        uint32_t row = coded >> 2u;
        return tag < 3u ? ((uint32_t)tables[tag] << 24) | row : 0u;
    }

    static uint32_t has_field_marshal_parent_to_token(uint32_t coded)
    {
        static const uint8_t tables[] = { 0x04u, 0x08u };
        uint32_t tag = coded & 0x1u;
        uint32_t row = coded >> 1u;
        return ((uint32_t)tables[tag] << 24) | row;
    }

    static struct zaclr_result get_metadata_name(const struct zaclr_metadata_reader* reader,
                                                 struct zaclr_token token,
                                                 struct zaclr_name_view* out_name)
    {
        if (reader == NULL || out_name == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            struct zaclr_typedef_row row = {};
            struct zaclr_result result = zaclr_metadata_reader_get_typedef_row(reader, zaclr_token_row(&token), &row);
            return result.status == ZACLR_STATUS_OK ? zaclr_metadata_reader_get_string(reader, row.name_index, out_name) : result;
        }

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
        {
            struct zaclr_field_row row = {};
            struct zaclr_result result = zaclr_metadata_reader_get_field_row(reader, zaclr_token_row(&token), &row);
            return result.status == ZACLR_STATUS_OK ? zaclr_metadata_reader_get_string(reader, row.name_index, out_name) : result;
        }

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_METHOD))
        {
            struct zaclr_methoddef_row row = {};
            struct zaclr_result result = zaclr_metadata_reader_get_methoddef_row(reader, zaclr_token_row(&token), &row);
            return result.status == ZACLR_STATUS_OK ? zaclr_metadata_reader_get_string(reader, row.name_index, out_name) : result;
        }

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_MEMBERREF))
        {
            struct zaclr_memberref_row row = {};
            struct zaclr_result result = zaclr_metadata_reader_get_memberref_row(reader, zaclr_token_row(&token), &row);
            return result.status == ZACLR_STATUS_OK ? zaclr_metadata_reader_get_string(reader, row.name_index, out_name) : result;
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetMetadataImport___STATIC__I__CLASS_System_Reflection_RuntimeModule(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle module_handle = 0u;
    const struct zaclr_loaded_assembly* corelib;
    struct zaclr_result result;
    uintptr_t native_module = 0u;
    uintptr_t native_assembly = 0u;
    struct zaclr_token field_token = {};

    result = zaclr_native_call_frame_arg_object(&frame, 0u, &module_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (frame.runtime == NULL || module_handle == 0u)
    {
        return zaclr_native_call_frame_set_i8(&frame, 0);
    }

    corelib = find_corelib(frame.runtime);
    if (corelib != NULL
        && get_field_token(corelib, "System.Reflection", "RuntimeModule", "m_pData", &field_token).status == ZACLR_STATUS_OK)
    {
        if (load_intptr_field(frame.runtime, module_handle, field_token, &native_module).status == ZACLR_STATUS_OK
            && native_module != 0u)
        {
            return zaclr_native_call_frame_set_i8(&frame, (int64_t)(uintptr_t)&((struct zaclr_loaded_assembly*)native_module)->metadata);
        }
    }

    if (corelib != NULL
        && get_field_token(corelib, "System.Reflection", "RuntimeAssembly", "m_assembly", &field_token).status == ZACLR_STATUS_OK)
    {
        if (load_intptr_field(frame.runtime, module_handle, field_token, &native_assembly).status == ZACLR_STATUS_OK
            && native_assembly != 0u)
        {
            return zaclr_native_call_frame_set_i8(&frame, (int64_t)(uintptr_t)&((struct zaclr_loaded_assembly*)native_assembly)->metadata);
        }
    }

    return zaclr_native_call_frame_set_i8(&frame, 0);
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetName___STATIC__I4__I__I4__BYREF_PTR_U1(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_name_view name = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = get_metadata_name(reader, token, &name);
    if (result.status != ZACLR_STATUS_OK)
    {
        result = store_byref_raw_pointer(frame, 2u, NULL);
        return result.status == ZACLR_STATUS_OK
            ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL)
            : result;
    }

    result = store_byref_raw_pointer(frame, 2u, name.text);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, (int32_t)name.length)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetNamespace___STATIC__I4__I__I4__BYREF_PTR_U1(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    int32_t raw_token = 0;
    struct zaclr_token token = {};
    struct zaclr_typedef_row type_row = {};
    struct zaclr_name_view namespace_name = {};
    struct zaclr_result result;

    result = load_metadata_reader_argument(frame, 0u, &reader);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_native_call_frame_arg_i4(&frame, 1u, &raw_token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    token = zaclr_token_make((uint32_t)raw_token);
    if (reader == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF))
    {
        result = store_byref_raw_pointer(frame, 2u, NULL);
        return result.status == ZACLR_STATUS_OK
            ? zaclr_native_call_frame_set_i4(&frame, 0)
            : result;
    }

    result = zaclr_metadata_reader_get_typedef_row(reader,
                                                   zaclr_token_row(&token),
                                                   &type_row);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_metadata_reader_get_string(reader, type_row.namespace_index, &namespace_name);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = store_byref_raw_pointer(frame, 2u, namespace_name.text);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)namespace_name.length);
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetUserString___STATIC__I4__I__I4__BYREF_PTR_CHAR__BYREF_I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    uint32_t row = zaclr_token_row(&token);

    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL || zaclr_token_table(&token) != 0x70u || row >= reader->user_string_heap.size)
    {
        result = store_byref_raw_pointer(frame, 2u, NULL);
        if (result.status == ZACLR_STATUS_OK)
        {
            result = store_byref_i4_raw(frame, 3u, 0);
        }
        return result.status == ZACLR_STATUS_OK
            ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL)
            : result;
    }

    /* User-string heap entries are compressed-length prefixed UTF-16 payloads with
       one trailing terminal byte.  Decode the subset needed by CoreLib reflection. */
    {
        const uint8_t* data = reader->user_string_heap.data + row;
        uint32_t available = reader->user_string_heap.size - row;
        uint32_t header_size = 1u;
        uint32_t payload_length = 0u;

        if (available == 0u)
        {
            return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
        }

        if ((data[0] & 0x80u) == 0u)
        {
            payload_length = data[0];
        }
        else if ((data[0] & 0xC0u) == 0x80u && available >= 2u)
        {
            header_size = 2u;
            payload_length = (((uint32_t)(data[0] & 0x3Fu)) << 8) | data[1];
        }
        else if ((data[0] & 0xE0u) == 0xC0u && available >= 4u)
        {
            header_size = 4u;
            payload_length = (((uint32_t)(data[0] & 0x1Fu)) << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
        }
        else
        {
            return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
        }

        if (header_size + payload_length > available)
        {
            return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
        }

        result = store_byref_raw_pointer(frame, 2u, data + header_size);
        if (result.status == ZACLR_STATUS_OK)
        {
            result = store_byref_i4_raw(frame, 3u, (int32_t)(payload_length / sizeof(uint16_t)));
        }
        return result.status == ZACLR_STATUS_OK
            ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
            : result;
    }
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetDefaultValue___STATIC__I4__I__I4__BYREF_I8__BYREF_PTR_CHAR__BYREF_I4__BYREF_I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    uint32_t target_token;
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = store_byref_i8_raw(frame, 2u, 0);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_raw_pointer(frame, 3u, NULL);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_i4_raw(frame, 4u, 0);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_i4_raw(frame, 5u, ZACLR_ELEMENT_TYPE_END);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
    }

    target_token = token.raw;
    for (uint32_t row_index = 1u; row_index <= zaclr_metadata_reader_get_row_count(reader, 0x0Bu); ++row_index)
    {
        struct zaclr_constant_row row = {};
        struct zaclr_slice blob = {};
        uint64_t raw_value = 0u;
        result = zaclr_metadata_reader_get_constant_row(reader, row_index, &row);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (has_constant_parent_to_token(row.parent_coded_index) != target_token)
        {
            continue;
        }

        result = zaclr_metadata_reader_get_blob(reader, row.value_blob_index, &blob);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (row.type == ZACLR_ELEMENT_TYPE_STRING)
        {
            result = store_byref_raw_pointer(frame, 3u, blob.data);
            if (result.status == ZACLR_STATUS_OK)
            {
                result = store_byref_i4_raw(frame, 4u, (int32_t)(blob.size / sizeof(uint16_t)));
            }
        }
        else if (blob.data != NULL && blob.size != 0u)
        {
            kernel_memcpy(&raw_value, blob.data, blob.size < sizeof(raw_value) ? blob.size : sizeof(raw_value));
            result = store_byref_i8_raw(frame, 2u, (int64_t)raw_value);
        }
        if (result.status == ZACLR_STATUS_OK)
        {
            result = store_byref_i4_raw(frame, 5u, (int32_t)(row.type & 0xFFu));
        }
        return result.status == ZACLR_STATUS_OK
            ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
            : result;
    }

    return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK);
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetEventProps___STATIC__I4__I__I4__BYREF_PTR_VOID__BYREF_I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_event_row row = {};
    struct zaclr_name_view name = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL || zaclr_token_table(&token) != 0x14u)
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
    }

    result = zaclr_metadata_reader_get_event_row(reader, zaclr_token_row(&token), &row);
    if (result.status == ZACLR_STATUS_OK) result = zaclr_metadata_reader_get_string(reader, row.name_index, &name);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_raw_pointer(frame, 2u, name.text);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_i4_raw(frame, 3u, (int32_t)row.flags);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetFieldDefProps___STATIC__I4__I__I4__BYREF_I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_field_row row = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
    }

    result = zaclr_metadata_reader_get_field_row(reader, zaclr_token_row(&token), &row);
    if (result.status == ZACLR_STATUS_OK)
    {
        result = store_byref_i4_raw(frame, 2u, (int32_t)row.flags);
    }
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetPropertyProps___STATIC__I4__I__I4__BYREF_PTR_VOID__BYREF_I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_property_row row = {};
    struct zaclr_name_view name = {};
    struct zaclr_slice signature = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL || zaclr_token_table(&token) != 0x17u)
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
    }

    result = zaclr_metadata_reader_get_property_row(reader, zaclr_token_row(&token), &row);
    if (result.status == ZACLR_STATUS_OK) result = zaclr_metadata_reader_get_string(reader, row.name_index, &name);
    if (result.status == ZACLR_STATUS_OK) result = zaclr_metadata_reader_get_blob(reader, row.type_blob_index, &signature);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_raw_pointer(frame, 2u, name.text);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_i4_raw(frame, 3u, (int32_t)row.flags);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_const_array(frame, 4u, signature.data, signature.size);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetParentToken___STATIC__I4__I__I4__BYREF_I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    int32_t parent_token = 0;
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
    }

    if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
    {
        for (uint32_t type_index = 1u; type_index <= zaclr_metadata_reader_get_row_count(reader, ZACLR_TOKEN_TABLE_TYPEDEF); ++type_index)
        {
            struct zaclr_typedef_row type_row = {};
            uint32_t next_field = zaclr_metadata_reader_get_row_count(reader, ZACLR_TOKEN_TABLE_FIELD) + 1u;
            result = zaclr_metadata_reader_get_typedef_row(reader, type_index, &type_row);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }
            if (type_index < zaclr_metadata_reader_get_row_count(reader, ZACLR_TOKEN_TABLE_TYPEDEF))
            {
                struct zaclr_typedef_row next_type = {};
                result = zaclr_metadata_reader_get_typedef_row(reader, type_index + 1u, &next_type);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
                next_field = next_type.field_list;
            }
            if (zaclr_token_row(&token) >= type_row.field_list && zaclr_token_row(&token) < next_field)
            {
                parent_token = (int32_t)(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | type_index);
                break;
            }
        }
    }
    else if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_METHOD))
    {
        for (uint32_t type_index = 1u; type_index <= zaclr_metadata_reader_get_row_count(reader, ZACLR_TOKEN_TABLE_TYPEDEF); ++type_index)
        {
            struct zaclr_typedef_row type_row = {};
            uint32_t next_method = zaclr_metadata_reader_get_row_count(reader, ZACLR_TOKEN_TABLE_METHOD) + 1u;
            result = zaclr_metadata_reader_get_typedef_row(reader, type_index, &type_row);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }
            if (type_index < zaclr_metadata_reader_get_row_count(reader, ZACLR_TOKEN_TABLE_TYPEDEF))
            {
                struct zaclr_typedef_row next_type = {};
                result = zaclr_metadata_reader_get_typedef_row(reader, type_index + 1u, &next_type);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
                next_method = next_type.method_list;
            }
            if (zaclr_token_row(&token) >= type_row.method_list && zaclr_token_row(&token) < next_method)
            {
                parent_token = (int32_t)(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | type_index);
                break;
            }
        }
    }

    result = store_byref_i4_raw(frame, 2u, parent_token);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetParamDefProps___STATIC__I4__I__I4__BYREF_I4__BYREF_I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_param_row row = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL || zaclr_token_table(&token) != 0x08u)
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
    }

    result = zaclr_metadata_reader_get_param_row(reader, zaclr_token_row(&token), &row);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_i4_raw(frame, 2u, (int32_t)row.sequence);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_i4_raw(frame, 3u, (int32_t)row.flags);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetGenericParamProps___STATIC__I4__I__I4__BYREF_I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_genericparam_row row = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL || zaclr_token_table(&token) != 0x2Au)
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
    }

    result = zaclr_metadata_reader_get_genericparam_row(reader, zaclr_token_row(&token), &row);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_i4_raw(frame, 2u, (int32_t)row.flags);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetScopeProps___STATIC__I4__I__BYREF_VALUETYPE_System_Guid(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_result result = load_metadata_reader_argument(frame, 0u, &reader);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    (void)reader;
    result = store_byref_guid_zero(frame, 1u);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetSigOfMethodDef___STATIC__I4__I__I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_methoddef_row row = {};
    struct zaclr_slice signature = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_METHOD))
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
    }

    result = zaclr_metadata_reader_get_methoddef_row(reader, zaclr_token_row(&token), &row);
    if (result.status == ZACLR_STATUS_OK) result = zaclr_metadata_reader_get_blob(reader, row.signature_blob_index, &signature);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_const_array(frame, 2u, signature.data, signature.size);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetSignatureFromToken___STATIC__I4__I__I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_slice signature = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL || zaclr_token_table(&token) != 0x11u)
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
    }

    {
        struct zaclr_standalonesig_row row = {};
        result = zaclr_metadata_reader_get_standalonesig_row(reader, zaclr_token_row(&token), &row);
        if (result.status == ZACLR_STATUS_OK) result = zaclr_metadata_reader_get_blob(reader, row.signature_blob_index, &signature);
        if (result.status == ZACLR_STATUS_OK) result = store_byref_const_array(frame, 2u, signature.data, signature.size);
    }
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetMemberRefProps___STATIC__I4__I__I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_memberref_row row = {};
    struct zaclr_slice signature = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_MEMBERREF))
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
    }

    result = zaclr_metadata_reader_get_memberref_row(reader, zaclr_token_row(&token), &row);
    if (result.status == ZACLR_STATUS_OK) result = zaclr_metadata_reader_get_blob(reader, row.signature_blob_index, &signature);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_const_array(frame, 2u, signature.data, signature.size);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetCustomAttributeProps___STATIC__I4__I__I4__BYREF_I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_customattribute_row row = {};
    struct zaclr_slice signature = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL || zaclr_token_table(&token) != 0x0Cu)
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
    }

    result = zaclr_metadata_reader_get_customattribute_row(reader, zaclr_token_row(&token), &row);
    if (result.status == ZACLR_STATUS_OK) result = zaclr_metadata_reader_get_blob(reader, row.value_blob_index, &signature);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_i4_raw(frame, 2u, (int32_t)methoddef_or_ref_coded_to_token(row.type_coded_index));
    if (result.status == ZACLR_STATUS_OK) result = store_byref_const_array(frame, 3u, signature.data, signature.size);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetClassLayout___STATIC__I4__I__I4__BYREF_I4__BYREF_I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    int32_t pack_size = 0;
    int32_t class_size = 0;
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader != NULL && zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF))
    {
        uint32_t target_row = zaclr_token_row(&token);
        for (uint32_t row_index = 1u; row_index <= zaclr_metadata_reader_get_row_count(reader, 0x0Fu); ++row_index)
        {
            struct zaclr_classlayout_row row = {};
            result = zaclr_metadata_reader_get_classlayout_row(reader, row_index, &row);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }
            if (row.parent == target_row)
            {
                pack_size = row.packing_size;
                class_size = (int32_t)row.class_size;
                break;
            }
        }
    }

    result = store_byref_i4_raw(frame, 2u, pack_size);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_i4_raw(frame, 3u, class_size);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetFieldOffset___STATIC__I4__I__I4__I4__BYREF_I4__BYREF_BOOLEAN(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token type_token = {};
    int32_t raw_field_token = 0;
    struct zaclr_token field_token = {};
    int32_t offset = 0;
    bool found = false;
    struct zaclr_result result = load_scope_and_token(frame, &reader, &type_token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }
    result = zaclr_native_call_frame_arg_i4(&frame, 2u, &raw_field_token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }
    field_token = zaclr_token_make((uint32_t)raw_field_token);

    if (reader != NULL && zaclr_token_matches_table(&field_token, ZACLR_TOKEN_TABLE_FIELD))
    {
        uint32_t target_row = zaclr_token_row(&field_token);
        for (uint32_t row_index = 1u; row_index <= zaclr_metadata_reader_get_row_count(reader, 0x10u); ++row_index)
        {
            struct zaclr_fieldlayout_row row = {};
            (void)type_token;
            result = zaclr_metadata_reader_get_fieldlayout_row(reader, row_index, &row);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }
            if (row.field == target_row)
            {
                offset = (int32_t)row.offset;
                found = true;
                break;
            }
        }
    }

    result = store_byref_i4_raw(frame, 3u, offset);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_bool_raw(frame, 4u, found);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetSigOfFieldDef___STATIC__I4__I__I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_field_row row = {};
    struct zaclr_slice signature = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (reader == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
    {
        return zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL);
    }

    result = zaclr_metadata_reader_get_field_row(reader, zaclr_token_row(&token), &row);
    if (result.status == ZACLR_STATUS_OK) result = zaclr_metadata_reader_get_blob(reader, row.signature_blob_index, &signature);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_const_array(frame, 2u, signature.data, signature.size);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetFieldMarshal___STATIC__I4__I__I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_slice signature = {};
    uint32_t target_token;
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    target_token = token.raw;
    if (reader != NULL)
    {
        for (uint32_t row_index = 1u; row_index <= zaclr_metadata_reader_get_row_count(reader, 0x0Du); ++row_index)
        {
            struct zaclr_fieldmarshal_row row = {};
            result = zaclr_metadata_reader_get_fieldmarshal_row(reader, row_index, &row);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }
            if (has_field_marshal_parent_to_token(row.parent_coded_index) == target_token)
            {
                result = zaclr_metadata_reader_get_blob(reader, row.native_type_blob_index, &signature);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
                break;
            }
        }
    }

    result = store_byref_const_array(frame, 2u, signature.data, signature.size);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::GetPInvokeMap___STATIC__I4__I__I4__BYREF_I4__BYREF_PTR_U1__BYREF_PTR_U1(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    uint32_t target_token;
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    target_token = token.raw;
    for (uint32_t row_index = 1u; reader != NULL && row_index <= zaclr_metadata_reader_get_row_count(reader, 0x1Cu); ++row_index)
    {
        struct zaclr_implmap_row row = {};
        struct zaclr_moduleref_row module = {};
        struct zaclr_name_view import_name = {};
        struct zaclr_name_view import_dll = {};
        uint32_t forwarded = row.member_forwarded_coded_index;
        result = zaclr_metadata_reader_get_implmap_row(reader, row_index, &row);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
        forwarded = ((row.member_forwarded_coded_index & 0x1u) == 0u ? ZACLR_TOKEN_TABLE_FIELD : ZACLR_TOKEN_TABLE_METHOD) << 24
                  | (row.member_forwarded_coded_index >> 1u);
        if (forwarded != target_token)
        {
            continue;
        }
        result = zaclr_metadata_reader_get_string(reader, row.import_name_index, &import_name);
        if (result.status == ZACLR_STATUS_OK) result = zaclr_metadata_reader_get_moduleref_row(reader, row.import_scope, &module);
        if (result.status == ZACLR_STATUS_OK) result = zaclr_metadata_reader_get_string(reader, module.name_index, &import_dll);
        if (result.status == ZACLR_STATUS_OK) result = store_byref_i4_raw(frame, 2u, (int32_t)row.flags);
        if (result.status == ZACLR_STATUS_OK) result = store_byref_raw_pointer(frame, 3u, import_name.text);
        if (result.status == ZACLR_STATUS_OK) result = store_byref_raw_pointer(frame, 4u, import_dll.text);
        return result.status == ZACLR_STATUS_OK
            ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_S_OK)
            : result;
    }

    result = store_byref_i4_raw(frame, 2u, 0);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_raw_pointer(frame, 3u, NULL);
    if (result.status == ZACLR_STATUS_OK) result = store_byref_raw_pointer(frame, 4u, NULL);
    return result.status == ZACLR_STATUS_OK
        ? zaclr_native_call_frame_set_i4(&frame, ZACLR_METADATA_IMPORT_E_FAIL)
        : result;
}

struct zaclr_result zaclr_native_System_Reflection_MetadataImport::IsValidToken___STATIC__BOOLEAN__I__I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_metadata_reader* reader = NULL;
    struct zaclr_token token = {};
    struct zaclr_result result = load_scope_and_token(frame, &reader, &token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_bool(&frame, metadata_token_is_valid(reader, token));
}
