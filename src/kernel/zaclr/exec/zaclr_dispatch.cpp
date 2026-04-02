#include <kernel/zaclr/exec/zaclr_dispatch.h>

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/exec/zaclr_emulated_float.h>
#include <kernel/zaclr/heap/zaclr_array.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/interop/zaclr_internal_call_registry.h>
#include <kernel/zaclr/interop/zaclr_marshalling.h>
#include <kernel/zaclr/interop/zaclr_native_assembly.h>
#include <kernel/zaclr/loader/zaclr_binder.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_member_resolution.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

extern "C" {
#include <kernel/support/kernel_memory.h>
}

namespace
{
    constexpr uint16_t k_method_impl_flag_internal_call = 0x1000u;

    constexpr int32_t k_int32_max = (int32_t)0x7FFFFFFF;
    constexpr int32_t k_int32_min = (int32_t)(-2147483647 - 1);
    constexpr uint32_t k_uint32_max = 0xFFFFFFFFu;
    constexpr int64_t k_int64_max = (int64_t)0x7FFFFFFFFFFFFFFFll;
    constexpr int64_t k_int64_min = (int64_t)(-0x7FFFFFFFFFFFFFFFll - 1ll);
    constexpr uint64_t k_uint64_max = 0xFFFFFFFFFFFFFFFFull;
    static uint16_t read_u16(const uint8_t* data)
    {
        return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    }

    struct zaclr_named_type_ref {
        const char* type_namespace;
        const char* type_name;
    };

    static struct zaclr_result append_signature_shape(struct zaclr_runtime* runtime,
                                                      const struct zaclr_loaded_assembly* current_assembly,
                                                      const struct zaclr_slice* blob,
                                                      uint32_t* offset,
                                                      char* buffer,
                                                      size_t capacity,
                                                      size_t* length);

    static uint32_t read_u32(const uint8_t* data)
    {
        return (uint32_t)data[0]
             | ((uint32_t)data[1] << 8)
             | ((uint32_t)data[2] << 16)
             | ((uint32_t)data[3] << 24);
    }

    static struct zaclr_result invoke_internal_call_exact(struct zaclr_dispatch_context* context,
                                                          struct zaclr_frame* frame,
                                                          const struct zaclr_loaded_assembly* owning_assembly,
                                                          const struct zaclr_type_desc* owning_type,
                                                          const struct zaclr_method_desc* method,
                                                          uint8_t invocation_kind);
    static struct zaclr_result resolve_type_desc(const struct zaclr_loaded_assembly* current_assembly,
                                                 struct zaclr_runtime* runtime,
                                                 struct zaclr_token token,
                                                 const struct zaclr_loaded_assembly** out_assembly,
                                                 const struct zaclr_type_desc** out_type);

    static uint64_t read_u64(const uint8_t* data)
    {
        return (uint64_t)read_u32(data)
             | ((uint64_t)read_u32(data + 4u) << 32);
    }

    static void write_u16(uint8_t* data, uint16_t value)
    {
        data[0] = (uint8_t)(value & 0xFFu);
        data[1] = (uint8_t)((value >> 8) & 0xFFu);
    }

    static void write_u32(uint8_t* data, uint32_t value)
    {
        data[0] = (uint8_t)(value & 0xFFu);
        data[1] = (uint8_t)((value >> 8) & 0xFFu);
        data[2] = (uint8_t)((value >> 16) & 0xFFu);
        data[3] = (uint8_t)((value >> 24) & 0xFFu);
    }

    static void write_u64(uint8_t* data, uint64_t value)
    {
        write_u32(data, (uint32_t)(value & 0xFFFFFFFFu));
        write_u32(data + 4u, (uint32_t)(value >> 32));
    }

    static bool text_equals(const char* left, const char* right)
    {
        return zaclr_text_equals(left, right);
    }

    static struct zaclr_result decode_compressed_uint(const uint8_t* data,
                                                      size_t size,
                                                      uint32_t* offset,
                                                      uint32_t* value)
    {
        uint8_t first;

        if (data == NULL || offset == NULL || value == NULL || *offset >= size)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
        }

        first = data[*offset];
        if ((first & 0x80u) == 0u)
        {
            *value = first;
            *offset += 1u;
            return zaclr_result_ok();
        }

        if ((first & 0xC0u) == 0x80u)
        {
            if ((*offset + 1u) >= size)
            {
                return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
            }

            *value = (((uint32_t)(first & 0x3Fu)) << 8) | (uint32_t)data[*offset + 1u];
            *offset += 2u;
            return zaclr_result_ok();
        }

        if ((*offset + 3u) >= size)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *value = (((uint32_t)(first & 0x1Fu)) << 24)
               | ((uint32_t)data[*offset + 1u] << 16)
               | ((uint32_t)data[*offset + 2u] << 8)
               | (uint32_t)data[*offset + 3u];
        *offset += 4u;
        return zaclr_result_ok();
    }

    static struct zaclr_result metadata_get_user_string_handle(struct zaclr_heap* heap,
                                                               const struct zaclr_metadata_reader* reader,
                                                               uint32_t index,
                                                               zaclr_object_handle* out_handle)
    {
        uint32_t offset = index;
        uint32_t byte_length;
        struct zaclr_result result;
        uint32_t char_length;
        uint16_t* text;
        uint32_t char_index;

        if (heap == NULL || reader == NULL || out_handle == NULL || reader->user_string_heap.data == NULL || index >= reader->user_string_heap.size)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = decode_compressed_uint(reader->user_string_heap.data, reader->user_string_heap.size, &offset, &byte_length);
        if (result.status != ZACLR_STATUS_OK || byte_length == 0u || ((size_t)offset + byte_length) > reader->user_string_heap.size)
        {
            return result.status == ZACLR_STATUS_OK
                ? zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC)
                : result;
        }

        char_length = byte_length / 2u;
        text = (uint16_t*)kernel_alloc(sizeof(uint16_t) * (size_t)char_length);
        if (text == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_EXEC);
        }

        for (char_index = 0u; char_index < char_length; ++char_index)
        {
            text[char_index] = (uint16_t)read_u16(reader->user_string_heap.data + offset + (char_index * 2u));
        }

        result = zaclr_string_allocate_utf16(heap, text, char_length, out_handle);
        kernel_free(text);
        return result;
    }

    static struct zaclr_result resolve_exported_type_forwarder(const struct zaclr_loaded_assembly* assembly,
                                                               struct zaclr_runtime* runtime,
                                                               const struct zaclr_member_name_ref* name,
                                                               const struct zaclr_loaded_assembly** out_assembly,
                                                               const struct zaclr_type_desc** out_type)
    {
        return zaclr_type_system_resolve_exported_type_forwarder(assembly, runtime, name, out_assembly, out_type);
    }

    static struct zaclr_result metadata_get_type_name(const struct zaclr_loaded_assembly* assembly,
                                                      struct zaclr_token token,
                                                      struct zaclr_member_name_ref* out_name)
    {
        return zaclr_metadata_get_type_name(assembly, token, out_name);
    }

    static uint32_t element_size_for_type_name(const struct zaclr_member_name_ref* type_name)
    {
        if (type_name == NULL || type_name->type_name == NULL)
        {
            return sizeof(zaclr_object_handle);
        }

        if (text_equals(type_name->type_name, "Byte")
            || text_equals(type_name->type_name, "SByte")
            || text_equals(type_name->type_name, "Boolean"))
        {
            return 1u;
        }

        if (text_equals(type_name->type_name, "Int16")
            || text_equals(type_name->type_name, "UInt16")
            || text_equals(type_name->type_name, "Char"))
        {
            return 2u;
        }

        if (text_equals(type_name->type_name, "Int64")
            || text_equals(type_name->type_name, "UInt64")
            || text_equals(type_name->type_name, "Double"))
        {
            return 8u;
        }

        if (text_equals(type_name->type_name, "Int32")
            || text_equals(type_name->type_name, "UInt32")
            || text_equals(type_name->type_name, "Single"))
        {
            return 4u;
        }

        if (text_equals(type_name->type_name, "IntPtr")
            || text_equals(type_name->type_name, "UIntPtr"))
        {
            return sizeof(uintptr_t);
        }

        return sizeof(zaclr_object_handle);
    }

    static uint32_t element_size_for_token(const struct zaclr_loaded_assembly* assembly,
                                           struct zaclr_token token)
    {
        struct zaclr_member_name_ref type_name = {};
        struct zaclr_result result;

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPESPEC))
        {
            return 4u;
        }

        result = metadata_get_type_name(assembly, token, &type_name);
        if (result.status != ZACLR_STATUS_OK)
        {
            return sizeof(zaclr_object_handle);
        }

        return element_size_for_type_name(&type_name);
    }

    static bool token_is_heap_reference(struct zaclr_token token)
    {
        return zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF)
            || zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEREF)
            || zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPESPEC);
    }

    static const struct zaclr_opcode_desc* find_opcode_desc(enum zaclr_opcode opcode)
    {
        const struct zaclr_opcode_desc* table = zaclr_opcode_table_get();
        size_t index;

        for (index = 0u; index < zaclr_opcode_table_count(); ++index)
        {
            if (table[index].opcode == opcode)
            {
                return &table[index];
            }
        }

        return NULL;
    }

    static struct zaclr_result push_i4(struct zaclr_frame* frame, int32_t value)
    {
        struct zaclr_stack_value stack_value = {};
        stack_value.kind = ZACLR_STACK_VALUE_I4;
        stack_value.data.i4 = value;
        return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
    }

    static struct zaclr_result push_i8(struct zaclr_frame* frame, int64_t value)
    {
        struct zaclr_stack_value stack_value = {};
        stack_value.kind = ZACLR_STACK_VALUE_I8;
        stack_value.data.i8 = value;
        return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
    }

    static struct zaclr_result push_r4(struct zaclr_frame* frame, uint32_t bits)
    {
        struct zaclr_stack_value stack_value = {};
        stack_value.kind = ZACLR_STACK_VALUE_R4;
        stack_value.data.r4_bits = bits;
        return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
    }

    static struct zaclr_result push_r8(struct zaclr_frame* frame, uint64_t bits)
    {
        struct zaclr_stack_value stack_value = {};
        stack_value.kind = ZACLR_STACK_VALUE_R8;
        stack_value.data.r8_bits = bits;
        return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
    }

    static struct zaclr_result load_local(struct zaclr_frame* frame, uint32_t local_index)
    {
        if (frame == NULL || local_index >= frame->local_count)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        return zaclr_eval_stack_push(&frame->eval_stack, &frame->locals[local_index]);
    }

    static struct zaclr_result load_argument(struct zaclr_frame* frame, uint32_t argument_index)
    {
        if (frame == NULL || argument_index >= frame->argument_count)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        return zaclr_eval_stack_push(&frame->eval_stack, &frame->arguments[argument_index]);
    }

    static struct zaclr_result load_static_field(struct zaclr_frame* frame, struct zaclr_token token)
    {
        uint32_t field_row;
        const struct zaclr_stack_value* value;
        struct zaclr_memberref_target memberref = {};

        if (frame == NULL || frame->assembly == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_MEMBERREF))
        {
            struct zaclr_result result = zaclr_metadata_get_memberref_info(frame->assembly, token, &memberref);
            const struct zaclr_loaded_assembly* target_assembly = NULL;
            uint32_t target_field_row = 0u;

            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_member_resolution_resolve_field(frame->runtime,
                                                           &memberref,
                                                           &target_assembly,
                                                           &target_field_row);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (target_field_row == 0u || target_field_row > target_assembly->static_field_count || target_assembly->static_fields == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
            }

            value = &target_assembly->static_fields[target_field_row - 1u];
            if (value->kind == ZACLR_STACK_VALUE_EMPTY)
            {
                return push_i4(frame, 0);
            }

            return zaclr_eval_stack_push(&frame->eval_stack, value);
        }

        if (!zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        field_row = zaclr_token_row(&token);
        if (field_row == 0u || field_row > frame->assembly->static_field_count || frame->assembly->static_fields == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
        }

        value = &frame->assembly->static_fields[field_row - 1u];
        if (value->kind == ZACLR_STACK_VALUE_EMPTY)
        {
            return push_i4(frame, 0);
        }

        return zaclr_eval_stack_push(&frame->eval_stack, value);
    }

    static struct zaclr_result store_local(struct zaclr_frame* frame, uint32_t local_index)
    {
        if (frame == NULL || local_index >= frame->local_count)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        return zaclr_eval_stack_pop(&frame->eval_stack, &frame->locals[local_index]);
    }

    static struct zaclr_result store_argument(struct zaclr_frame* frame, uint32_t argument_index)
    {
        if (frame == NULL || argument_index >= frame->argument_count)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        return zaclr_eval_stack_pop(&frame->eval_stack, &frame->arguments[argument_index]);
    }

    static struct zaclr_result store_static_field(struct zaclr_frame* frame, struct zaclr_token token)
    {
        uint32_t field_row;

        if (frame == NULL || frame->assembly == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        field_row = zaclr_token_row(&token);
        if (field_row == 0u || field_row > frame->assembly->static_field_count || frame->assembly->static_fields == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
        }

        return zaclr_eval_stack_pop(&frame->eval_stack, &frame->assembly->static_fields[field_row - 1u]);
    }

    static struct zaclr_result store_object_field(struct zaclr_runtime* runtime,
                                                  zaclr_object_handle handle,
                                                  struct zaclr_token token,
                                                  const struct zaclr_stack_value* value)
    {
        return zaclr_object_store_field(runtime, handle, token, value);
    }

    static struct zaclr_result push_object_handle(struct zaclr_frame* frame, zaclr_object_handle value)
    {
        struct zaclr_stack_value stack_value = {};
        stack_value.kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
        stack_value.data.object_handle = value;
        return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
    }

    static struct zaclr_result push_local_address(struct zaclr_frame* frame, uint32_t local_index)
    {
        struct zaclr_stack_value stack_value = {};
        stack_value.kind = ZACLR_STACK_VALUE_LOCAL_ADDRESS;
        stack_value.data.raw = local_index;
        return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
    }

    static struct zaclr_stack_value* resolve_local_address_target(struct zaclr_frame* frame,
                                                                  struct zaclr_stack_value* value)
    {
        uint32_t local_index;

        if (frame == NULL || value == NULL || value->kind != ZACLR_STACK_VALUE_LOCAL_ADDRESS)
        {
            return NULL;
        }

        local_index = (uint32_t)value->data.raw;
        if (local_index < frame->local_count)
        {
            return &frame->locals[local_index];
        }

        return (struct zaclr_stack_value*)(uintptr_t)value->data.raw;
    }

    static struct zaclr_result box_stack_value(struct zaclr_runtime* runtime,
                                               const struct zaclr_stack_value* value,
                                               struct zaclr_token token,
                                               zaclr_object_handle* out_handle)
    {
        if (runtime == NULL || value == NULL || out_handle == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        return zaclr_boxed_value_allocate(&runtime->heap, token, value, out_handle);
    }

    static struct zaclr_result allocate_reference_type_instance(struct zaclr_runtime* runtime,
                                                                const struct zaclr_loaded_assembly* owning_assembly,
                                                                struct zaclr_token type_token,
                                                                zaclr_object_handle* out_handle)
    {
        zaclr_type_id type_id;
        uint32_t field_capacity = 0u;
        const struct zaclr_type_desc* type_desc = NULL;

        auto accumulate_instance_field_capacity = [&](const struct zaclr_loaded_assembly* current_assembly,
                                                      const struct zaclr_type_desc* current_type,
                                                      uint32_t* io_capacity,
                                                      auto&& self) -> struct zaclr_result
        {
            const struct zaclr_loaded_assembly* base_assembly;
            const struct zaclr_type_desc* base_type;
            struct zaclr_result result;

            if (current_assembly == NULL || current_type == NULL || io_capacity == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
            }

            *io_capacity += current_type->field_count;
            if (zaclr_token_is_nil(&current_type->extends))
            {
                return zaclr_result_ok();
            }

            result = resolve_type_desc(current_assembly,
                                       runtime,
                                       current_type->extends,
                                       &base_assembly,
                                       &base_type);
            if (result.status != ZACLR_STATUS_OK || base_type == NULL)
            {
                return result.status == ZACLR_STATUS_OK
                    ? zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC)
                    : result;
            }

            return self(base_assembly, base_type, io_capacity, self);
        };

        if (runtime == NULL || out_handle == NULL || owning_assembly == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_handle = 0u;
        if (zaclr_token_matches_table(&type_token, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            type_id = zaclr_token_row(&type_token);
            type_desc = zaclr_type_map_find_by_token(&owning_assembly->type_map, type_token);
            if (type_desc == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
            }

            {
                struct zaclr_result result = accumulate_instance_field_capacity(owning_assembly,
                                                                                type_desc,
                                                                                &field_capacity,
                                                                                accumulate_instance_field_capacity);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

            }
        }
        else if (token_is_heap_reference(type_token))
        {
            type_id = 0u;
        }
        else
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        {
            struct zaclr_result result = zaclr_reference_object_allocate(&runtime->heap, owning_assembly, type_id, type_token, field_capacity, out_handle);
            struct zaclr_object_desc* object;
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (type_desc != NULL
                && (zaclr_type_runtime_flags(type_desc) & ZACLR_TYPE_RUNTIME_FLAG_HAS_FINALIZER) != 0u)
            {
                object = zaclr_heap_get_object(&runtime->heap, *out_handle);
                if (object != NULL)
                {
                    object->gc_state = (uint8_t)(object->gc_state | ZACLR_OBJECT_GC_STATE_FINALIZER_PENDING);
                }
            }

            return zaclr_result_ok();
        }
    }

    static bool token_is_named_type(const struct zaclr_loaded_assembly* assembly,
                                    struct zaclr_token token,
                                    const char* type_namespace,
                                    const char* type_name)
    {
        struct zaclr_member_name_ref name = {};
        struct zaclr_result result;

        result = metadata_get_type_name(assembly, token, &name);
        return result.status == ZACLR_STATUS_OK
            && text_equals(name.type_namespace, type_namespace)
            && text_equals(name.type_name, type_name);
    }

    static struct zaclr_result append_signature_shape(struct zaclr_runtime* runtime,
                                                      const struct zaclr_loaded_assembly* current_assembly,
                                                      const struct zaclr_slice* blob,
                                                      uint32_t* offset,
                                                      char* buffer,
                                                      size_t capacity,
                                                      size_t* length);

    static struct zaclr_result resolve_type_desc(const struct zaclr_loaded_assembly* current_assembly,
                                                 struct zaclr_runtime* runtime,
                                                 struct zaclr_token token,
                                                 const struct zaclr_loaded_assembly** out_assembly,
                                                 const struct zaclr_type_desc** out_type)
    {
        if (current_assembly == NULL || runtime == NULL || out_assembly == NULL || out_type == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_assembly = NULL;
        *out_type = NULL;

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            *out_assembly = current_assembly;
            *out_type = zaclr_type_map_find_by_token(&current_assembly->type_map, token);
            return *out_type != NULL
                ? zaclr_result_ok()
                : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEREF))
        {
            struct zaclr_member_name_ref name = {};
            const char* assembly_name = NULL;
            struct zaclr_result result = zaclr_metadata_get_typeref_name(&current_assembly->metadata, zaclr_token_row(&token), &name);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_metadata_get_typeref_assembly_name(&current_assembly->metadata, zaclr_token_row(&token), &assembly_name);
            if (result.status != ZACLR_STATUS_OK || assembly_name == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
            }

            result = zaclr_binder_load_assembly_by_name(runtime, assembly_name, out_assembly);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            *out_type = zaclr_type_system_find_type_by_name(*out_assembly, &name);
            if (*out_type != NULL)
            {
                return zaclr_result_ok();
            }

            return resolve_exported_type_forwarder(*out_assembly,
                                                   runtime,
                                                   &name,
                                                   out_assembly,
                                                   out_type);
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    static bool append_shape_text(char* buffer, size_t capacity, size_t* length, const char* text)
    {
        size_t index;

        if (buffer == NULL || length == NULL || text == NULL)
        {
            return false;
        }

        for (index = 0u; text[index] != '\0'; ++index)
        {
            if ((*length + 1u) >= capacity)
            {
                return false;
            }

            buffer[*length] = text[index];
            ++(*length);
        }

        buffer[*length] = '\0';
        return true;
    }

    static struct zaclr_result append_signature_shape(struct zaclr_runtime* runtime,
                                                      const struct zaclr_loaded_assembly* current_assembly,
                                                      const struct zaclr_slice* blob,
                                                      uint32_t* offset,
                                                      char* buffer,
                                                      size_t capacity,
                                                      size_t* length)
    {
        uint8_t element_type;

        if (runtime == NULL || current_assembly == NULL || blob == NULL || blob->data == NULL || offset == NULL || *offset >= blob->size)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        element_type = blob->data[*offset];
        *offset += 1u;

        while (element_type == ZACLR_ELEMENT_TYPE_BYREF || element_type == ZACLR_ELEMENT_TYPE_PINNED)
        {
            if (!append_shape_text(buffer, capacity, length, element_type == ZACLR_ELEMENT_TYPE_BYREF ? "BYREF_" : "PINNED_"))
            {
                return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            }

            if (*offset >= blob->size)
            {
                return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
            }

            element_type = blob->data[*offset];
            *offset += 1u;
        }

        switch (element_type)
        {
            case ZACLR_ELEMENT_TYPE_VOID: return append_shape_text(buffer, capacity, length, "VOID") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_BOOLEAN: return append_shape_text(buffer, capacity, length, "BOOLEAN") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_CHAR: return append_shape_text(buffer, capacity, length, "CHAR") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_I1: return append_shape_text(buffer, capacity, length, "I1") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_U1: return append_shape_text(buffer, capacity, length, "U1") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_I2: return append_shape_text(buffer, capacity, length, "I2") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_U2: return append_shape_text(buffer, capacity, length, "U2") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_I4: return append_shape_text(buffer, capacity, length, "I4") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_U4: return append_shape_text(buffer, capacity, length, "U4") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_I8: return append_shape_text(buffer, capacity, length, "I8") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_U8: return append_shape_text(buffer, capacity, length, "U8") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_R4: return append_shape_text(buffer, capacity, length, "R4") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_R8: return append_shape_text(buffer, capacity, length, "R8") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_STRING: return append_shape_text(buffer, capacity, length, "STRING") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_OBJECT: return append_shape_text(buffer, capacity, length, "OBJECT") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_I: return append_shape_text(buffer, capacity, length, "I") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            case ZACLR_ELEMENT_TYPE_U: return append_shape_text(buffer, capacity, length, "U") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);

            case ZACLR_ELEMENT_TYPE_SZARRAY:
                if (!append_shape_text(buffer, capacity, length, "SZARRAY_"))
                {
                    return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
                }
                return append_signature_shape(runtime, current_assembly, blob, offset, buffer, capacity, length);

            case ZACLR_ELEMENT_TYPE_PTR:
                if (!append_shape_text(buffer, capacity, length, "PTR_"))
                {
                    return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
                }
                return append_signature_shape(runtime, current_assembly, blob, offset, buffer, capacity, length);

            case ZACLR_ELEMENT_TYPE_CLASS:
            case ZACLR_ELEMENT_TYPE_VALUETYPE:
            {
                uint32_t coded_value;
                uint32_t token_table;
                struct zaclr_result result = decode_compressed_uint(blob->data, blob->size, offset, &coded_value);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                token_table = coded_value & 0x3u;
                {
                    struct zaclr_token token = zaclr_token_make(((token_table == 0u ? ZACLR_TOKEN_TABLE_TYPEDEF
                                                               : (token_table == 1u ? ZACLR_TOKEN_TABLE_TYPEREF : ZACLR_TOKEN_TABLE_TYPESPEC))
                                                              << 24)
                                                             | (coded_value >> 2));
                    const struct zaclr_loaded_assembly* resolved_assembly = NULL;
                    const struct zaclr_type_desc* resolved_type = NULL;
                    result = resolve_type_desc(current_assembly, runtime, token, &resolved_assembly, &resolved_type);
                    if (result.status == ZACLR_STATUS_OK && resolved_type != NULL)
                    {
                        if (text_equals(resolved_type->type_namespace.text, "System") && text_equals(resolved_type->type_name.text, "String"))
                        {
                            return append_shape_text(buffer, capacity, length, "STRING") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
                        }

                        if (text_equals(resolved_type->type_namespace.text, "System") && text_equals(resolved_type->type_name.text, "Object"))
                        {
                            return append_shape_text(buffer, capacity, length, "OBJECT") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
                        }

                        if (text_equals(resolved_type->type_namespace.text, "System") && text_equals(resolved_type->type_name.text, "Char") && element_type == ZACLR_ELEMENT_TYPE_VALUETYPE)
                        {
                            return append_shape_text(buffer, capacity, length, "CHAR") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
                        }
                    }
                }

                return append_shape_text(buffer, capacity, length, element_type == ZACLR_ELEMENT_TYPE_CLASS ? "CLASS" : "VALUETYPE") ? zaclr_result_ok() : zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
            }

            default:
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }
    }

    static bool reference_type_matches_target(const struct zaclr_loaded_assembly* source_assembly,
                                              const struct zaclr_loaded_assembly* target_assembly,
                                              const struct zaclr_type_desc* source_type,
                                              const struct zaclr_type_desc* target_type)
    {
        const struct zaclr_type_desc* current_type = source_type;

        while (current_type != NULL)
        {
            if (source_assembly == target_assembly && current_type->token.raw == target_type->token.raw)
            {
                return true;
            }

            if (!zaclr_token_matches_table(&current_type->extends, ZACLR_TOKEN_TABLE_TYPEDEF))
            {
                break;
            }

            current_type = zaclr_type_map_find_by_token(&source_assembly->type_map, current_type->extends);
        }

        return false;
    }

    static bool object_matches_type(struct zaclr_runtime* runtime,
                                    const struct zaclr_loaded_assembly* current_assembly,
                                    zaclr_object_handle handle,
                                    struct zaclr_token target_token)
    {
        struct zaclr_object_desc* object;
        uint32_t flags;

        if (runtime == NULL || current_assembly == NULL || handle == 0u)
        {
            return false;
        }

        object = zaclr_heap_get_object(&runtime->heap, handle);
        if (object == NULL)
        {
            return false;
        }

        if (token_is_named_type(current_assembly, target_token, "System", "Object"))
        {
            return true;
        }

        flags = zaclr_object_flags(object);
        if ((flags & ZACLR_OBJECT_FLAG_STRING) != 0u)
        {
            return token_is_named_type(current_assembly, target_token, "System", "String");
        }

        if ((flags & ZACLR_OBJECT_FLAG_BOXED_VALUE) != 0u)
        {
            const struct zaclr_boxed_value_desc* boxed_value = (const struct zaclr_boxed_value_desc*)object;
            return boxed_value->type_token_raw == target_token.raw;
        }

        if ((flags & ZACLR_OBJECT_FLAG_REFERENCE_TYPE) != 0u)
        {
            const struct zaclr_loaded_assembly* target_assembly;
            const struct zaclr_type_desc* source_type;
            const struct zaclr_type_desc* target_type;
            struct zaclr_token source_token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | object->type_id);
            struct zaclr_result result = resolve_type_desc(current_assembly, runtime, target_token, &target_assembly, &target_type);
            if (result.status != ZACLR_STATUS_OK)
            {
                return false;
            }

            source_type = zaclr_type_map_find_by_token(&current_assembly->type_map, source_token);
            if (source_type == NULL)
            {
                return false;
            }

            return reference_type_matches_target(current_assembly, target_assembly, source_type, target_type);
        }

        return false;
    }

    static struct zaclr_result load_object_field(struct zaclr_runtime* runtime,
                                                 zaclr_object_handle handle,
                                                 struct zaclr_token token,
                                                 struct zaclr_stack_value* out_value)
    {
        return zaclr_object_load_field(runtime, handle, token, out_value);
    }

    static struct zaclr_result bind_newobj_arguments(struct zaclr_frame* child,
                                                     struct zaclr_eval_stack* caller_stack,
                                                     zaclr_object_handle instance_handle)
    {
        uint32_t argument_index;

        if (child == NULL || caller_stack == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if ((child->method != NULL) && ((child->method->signature.calling_convention & 0x20u) != 0u))
        {
            if (child->argument_count == 0u)
            {
                return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
            }

            if (caller_stack->depth < (uint32_t)(child->argument_count - 1u))
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            child->arguments[0].kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
            child->arguments[0].data.object_handle = instance_handle;
            for (argument_index = child->argument_count; argument_index > 1u; --argument_index)
            {
                struct zaclr_result result = zaclr_eval_stack_pop(caller_stack, &child->arguments[argument_index - 1u]);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
            }

            return zaclr_result_ok();
        }

        return zaclr_frame_bind_arguments(child, caller_stack);
    }

    static bool is_system_object_ctor(const struct zaclr_memberref_target* memberref)
    {
        return memberref != NULL
            && text_equals(memberref->key.type_namespace, "System")
            && text_equals(memberref->key.type_name, "Object")
            && text_equals(memberref->key.method_name, ".ctor");
    }

    static struct zaclr_result unbox_any_value(struct zaclr_runtime* runtime,
                                               zaclr_object_handle handle,
                                               struct zaclr_token token,
                                               struct zaclr_stack_value* out_value)
    {
        struct zaclr_boxed_value_desc* boxed_value;

        if (runtime == NULL || out_value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        boxed_value = zaclr_boxed_value_from_handle(&runtime->heap, handle);
        if (boxed_value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
        }

        if (boxed_value->type_token_raw != token.raw)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_value = boxed_value->value;
        return zaclr_result_ok();
    }

    static struct zaclr_result stack_value_to_i32(const struct zaclr_stack_value* value, int32_t* out_value)
    {
        if (value == NULL || out_value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
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

    static bool decode_encoded_gc_handle_value(uint64_t value, uint32_t* out_index)
    {
        uint64_t raw;

        if (out_index == NULL || value == 0u)
        {
            return false;
        }

        raw = value;
        if ((raw & 0x1u) != 0u)
        {
            return false;
        }

        raw >>= 1u;
        if (raw == 0u || raw > 0xFFFFFFFFu)
        {
            return false;
        }

        *out_index = (uint32_t)(raw - 1u);
        return true;
    }

    static struct zaclr_result load_indirect_ref(struct zaclr_runtime* runtime,
                                                 struct zaclr_frame* frame,
                                                 const struct zaclr_stack_value* address_value,
                                                 zaclr_object_handle* out_handle)
    {
        struct zaclr_stack_value* target;
        uint64_t encoded_handle = 0u;
        uint32_t handle_index;
        struct zaclr_gc_handle_entry entry = {};
        struct zaclr_result result;

        if (runtime == NULL || out_handle == NULL || address_value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_handle = 0u;
        if (address_value->kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
        {
            target = resolve_local_address_target(frame, (struct zaclr_stack_value*)address_value);
            if (target == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            if (target->kind != ZACLR_STACK_VALUE_OBJECT_HANDLE)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            *out_handle = target->data.object_handle;
            return zaclr_result_ok();
        }

        if (address_value->kind == ZACLR_STACK_VALUE_OBJECT_HANDLE)
        {
            *out_handle = address_value->data.object_handle;
            return zaclr_result_ok();
        }

        if (address_value->kind == ZACLR_STACK_VALUE_I8)
        {
            encoded_handle = (uint64_t)address_value->data.i8;
        }
        else if (address_value->kind == ZACLR_STACK_VALUE_I4)
        {
            encoded_handle = (uint32_t)address_value->data.i4;
        }
        else
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (encoded_handle == 0u)
        {
            *out_handle = 0u;
            return zaclr_result_ok();
        }

        if (!decode_encoded_gc_handle_value(encoded_handle, &handle_index))
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = zaclr_handle_table_load_entry(&runtime->boot_launch.handle_table, handle_index, &entry);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_handle = entry.handle;
        return zaclr_result_ok();
    }

    static struct zaclr_result stack_value_to_u32(const struct zaclr_stack_value* value, uint32_t* out_value)
    {
        int32_t signed_value;
        struct zaclr_result result = stack_value_to_i32(value, &signed_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_value = (uint32_t)signed_value;
        return zaclr_result_ok();
    }

    static struct zaclr_result stack_value_to_i64(const struct zaclr_stack_value* value, int64_t* out_value)
    {
        if (value == NULL || out_value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (value->kind == ZACLR_STACK_VALUE_I8)
        {
            *out_value = value->data.i8;
            return zaclr_result_ok();
        }

        if (value->kind == ZACLR_STACK_VALUE_I4)
        {
            *out_value = value->data.i4;
            return zaclr_result_ok();
        }

        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    static int64_t stack_value_to_common_float_bits(const struct zaclr_stack_value* value)
    {
        switch (value->kind)
        {
            case ZACLR_STACK_VALUE_R4:
#if defined(PLATFORM_EMULATED_FLOATINGPOINT)
                return ((int64_t)(int32_t)value->data.r4_bits) << (zaclr::emulated_float::k_float64_shift - zaclr::emulated_float::k_float32_shift);
#else
                return zaclr::emulated_float::decode_r4_to_common(value->data.r4_bits);
#endif
            case ZACLR_STACK_VALUE_R8:
#if defined(PLATFORM_EMULATED_FLOATINGPOINT)
                return (int64_t)value->data.r8_bits;
#else
                return zaclr::emulated_float::decode_r8_to_common(value->data.r8_bits);
#endif
            case ZACLR_STACK_VALUE_I4:
                return ((int64_t)value->data.i4) << zaclr::emulated_float::k_float64_shift;
            case ZACLR_STACK_VALUE_I8:
                return value->data.i8 << zaclr::emulated_float::k_float64_shift;
            default:
                return 0;
        }
    }

    static int64_t stack_value_to_common_float_bits_unsigned(const struct zaclr_stack_value* value)
    {
        switch (value->kind)
        {
            case ZACLR_STACK_VALUE_R4:
#if defined(PLATFORM_EMULATED_FLOATINGPOINT)
                return ((int64_t)(int32_t)value->data.r4_bits) << (zaclr::emulated_float::k_float64_shift - zaclr::emulated_float::k_float32_shift);
#else
                return zaclr::emulated_float::decode_r4_to_common(value->data.r4_bits);
#endif
            case ZACLR_STACK_VALUE_R8:
#if defined(PLATFORM_EMULATED_FLOATINGPOINT)
                return (int64_t)value->data.r8_bits;
#else
                return zaclr::emulated_float::decode_r8_to_common(value->data.r8_bits);
#endif
            case ZACLR_STACK_VALUE_I4:
                return ((int64_t)(uint32_t)value->data.i4) << zaclr::emulated_float::k_float64_shift;
            case ZACLR_STACK_VALUE_I8:
                return (int64_t)((uint64_t)value->data.i8 << zaclr::emulated_float::k_float64_shift);
            default:
                return 0;
        }
    }

    static struct zaclr_result push_common_float(struct zaclr_frame* frame, int64_t value, bool as_r8)
    {
        return as_r8 ? push_r8(frame, (uint64_t)value)
                     : push_r4(frame, (uint32_t)(value >> (zaclr::emulated_float::k_float64_shift - zaclr::emulated_float::k_float32_shift)));
    }

    static struct zaclr_result push_binary_r8_result(struct zaclr_frame* frame,
                                                     enum zaclr_opcode opcode,
                                                     const struct zaclr_stack_value* left,
                                                     const struct zaclr_stack_value* right)
    {
        int64_t left_common;
        int64_t right_common;
        int64_t result_common;
        bool as_r8;

        if (frame == NULL || left == NULL || right == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        left_common = stack_value_to_common_float_bits(left);
        right_common = stack_value_to_common_float_bits(right);
        as_r8 = left->kind == ZACLR_STACK_VALUE_R8 || right->kind == ZACLR_STACK_VALUE_R8;

        switch (opcode)
        {
            case CEE_ADD:
                result_common = left_common + right_common;
                break;
            case CEE_SUB:
                result_common = left_common - right_common;
                break;
            case CEE_MUL:
            {
                result_common = zaclr::emulated_float::multiply_common(left_common, right_common);
                break;
            }
            case CEE_DIV:
                result_common = right_common == 0 ? 0 : ((left_common / right_common) << zaclr::emulated_float::k_float64_shift);
                break;
            case CEE_REM:
                result_common = right_common == 0 ? 0 : (left_common % right_common);
                break;
            default:
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        return push_common_float(frame, result_common, as_r8);
    }

    static struct zaclr_result pop_branch_i4_pair(struct zaclr_frame* frame, int32_t* out_left, int32_t* out_right)
    {
        struct zaclr_stack_value right;
        struct zaclr_stack_value left;
        struct zaclr_result result;

        if (frame == NULL || out_left == NULL || out_right == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = zaclr_eval_stack_pop(&frame->eval_stack, &right);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_eval_stack_pop(&frame->eval_stack, &left);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (left.kind == ZACLR_STACK_VALUE_OBJECT_HANDLE && right.kind == ZACLR_STACK_VALUE_OBJECT_HANDLE)
        {
            *out_left = (int32_t)left.data.object_handle;
            *out_right = (int32_t)right.data.object_handle;
            return zaclr_result_ok();
        }

        result = stack_value_to_i32(&left, out_left);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return stack_value_to_i32(&right, out_right);
    }

    static struct zaclr_result pop_branch_u4_pair(struct zaclr_frame* frame, uint32_t* out_left, uint32_t* out_right)
    {
        struct zaclr_stack_value right;
        struct zaclr_stack_value left;
        struct zaclr_result result;

        if (frame == NULL || out_left == NULL || out_right == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = zaclr_eval_stack_pop(&frame->eval_stack, &right);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_eval_stack_pop(&frame->eval_stack, &left);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = stack_value_to_u32(&left, out_left);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return stack_value_to_u32(&right, out_right);
    }

    static bool stack_value_is_float(const struct zaclr_stack_value* value)
    {
        return value != NULL && (value->kind == ZACLR_STACK_VALUE_R4 || value->kind == ZACLR_STACK_VALUE_R8);
    }

    static struct zaclr_result pop_branch_float_pair(struct zaclr_frame* frame, int64_t* out_left, int64_t* out_right)
    {
        struct zaclr_stack_value right;
        struct zaclr_stack_value left;
        struct zaclr_result result;

        if (frame == NULL || out_left == NULL || out_right == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = zaclr_eval_stack_pop(&frame->eval_stack, &right);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_eval_stack_pop(&frame->eval_stack, &left);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_left = stack_value_to_common_float_bits(&left);
        *out_right = stack_value_to_common_float_bits(&right);
        return zaclr_result_ok();
    }

    static uint32_t compute_short_branch_target(uint32_t il_offset, int8_t delta)
    {
        return (uint32_t)((int32_t)il_offset + 2 + (int32_t)delta);
    }

    static uint32_t compute_inline_branch_target(uint32_t il_offset, int32_t delta)
    {
        return (uint32_t)((int32_t)il_offset + 5 + delta);
    }

    static struct zaclr_result stack_value_to_object_handle(const struct zaclr_stack_value* value,
                                                            zaclr_object_handle* out_value)
    {
        if (value == NULL || out_value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (value->kind != ZACLR_STACK_VALUE_OBJECT_HANDLE)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_value = value->data.object_handle;
        return zaclr_result_ok();
    }

    static struct zaclr_result push_binary_i4_result(struct zaclr_frame* frame,
                                                     enum zaclr_opcode opcode,
                                                     int32_t left,
                                                     int32_t right)
    {
        switch (opcode)
        {
            case CEE_ADD: return push_i4(frame, left + right);
            case CEE_SUB: return push_i4(frame, left - right);
            case CEE_MUL: return push_i4(frame, left * right);
            case CEE_DIV: return right == 0 ? zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC) : push_i4(frame, left / right);
            case CEE_DIV_UN: return right == 0 ? zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC) : push_i4(frame, (int32_t)((uint32_t)left / (uint32_t)right));
            case CEE_REM: return right == 0 ? zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC) : push_i4(frame, left % right);
            case CEE_REM_UN: return right == 0 ? zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC) : push_i4(frame, (int32_t)((uint32_t)left % (uint32_t)right));
            case CEE_AND: return push_i4(frame, left & right);
            case CEE_OR: return push_i4(frame, left | right);
            case CEE_XOR: return push_i4(frame, left ^ right);
            case CEE_SHL: return push_i4(frame, left << (right & 31));
            case CEE_SHR: return push_i4(frame, left >> (right & 31));
            case CEE_SHR_UN: return push_i4(frame, (int32_t)((uint32_t)left >> (right & 31)));
            default:
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }
    }

    static struct zaclr_result push_binary_i8_result(struct zaclr_frame* frame,
                                                     enum zaclr_opcode opcode,
                                                     int64_t left,
                                                     int64_t right)
    {
        switch (opcode)
        {
            case CEE_ADD: return push_i8(frame, left + right);
            case CEE_SUB: return push_i8(frame, left - right);
            case CEE_MUL: return push_i8(frame, left * right);
            case CEE_DIV: return right == 0 ? zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC) : push_i8(frame, left / right);
            case CEE_DIV_UN: return right == 0 ? zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC) : push_i8(frame, (int64_t)((uint64_t)left / (uint64_t)right));
            case CEE_REM: return right == 0 ? zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC) : push_i8(frame, left % right);
            case CEE_REM_UN: return right == 0 ? zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC) : push_i8(frame, (int64_t)((uint64_t)left % (uint64_t)right));
            case CEE_AND: return push_i8(frame, left & right);
            case CEE_OR: return push_i8(frame, left | right);
            case CEE_XOR: return push_i8(frame, left ^ right);
            case CEE_SHL: return push_i8(frame, left << (right & 63));
            case CEE_SHR: return push_i8(frame, left >> (right & 63));
            case CEE_SHR_UN: return push_i8(frame, (int64_t)((uint64_t)left >> (right & 63)));
            default:
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }
    }

    static struct zaclr_result create_managed_exception(struct zaclr_runtime* runtime,
                                                        zaclr_object_handle* out_handle)
    {
        if (runtime == NULL || out_handle == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_handle = 0u;
        {
            struct zaclr_object_desc* exception_object;
            struct zaclr_result result = zaclr_heap_allocate_object(&runtime->heap,
                                                                    sizeof(struct zaclr_object_desc),
                                                                    NULL,
                                                                    0u,
                                                                    ZACLR_OBJECT_FAMILY_INSTANCE,
                                                                    ZACLR_OBJECT_FLAG_REFERENCE_TYPE,
                                                                    &exception_object);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            *out_handle = exception_object->handle;
        }

        runtime->boot_launch.thread.current_exception = *out_handle;
        return zaclr_result_ok();
    }

    static bool does_catch_clause_match(const struct zaclr_frame* frame,
                                        const struct zaclr_exception_clause* clause,
                                        zaclr_object_handle exception_handle)
    {
        if (frame == NULL || clause == NULL)
        {
            return false;
        }

        if ((clause->flags & 0x0007u) != 0u)
        {
            return false;
        }

        if (clause->class_token == 0u)
        {
            return true;
        }
        (void)exception_handle;
        return true;
    }

    static struct zaclr_result dispatch_exception_to_handler(struct zaclr_dispatch_context* context,
                                                             struct zaclr_frame* frame,
                                                             zaclr_object_handle exception_handle)
    {
        uint16_t clause_index;

        if (context == NULL || frame == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        for (clause_index = 0u; clause_index < frame->exception_clause_count; ++clause_index)
        {
            const struct zaclr_exception_clause* clause = &frame->exception_clauses[clause_index];
            const uint32_t throw_offset = frame->il_offset;

            if (throw_offset < clause->try_offset || throw_offset >= (clause->try_offset + clause->try_length))
            {
                continue;
            }

            if (!does_catch_clause_match(frame, clause, exception_handle))
            {
                continue;
            }

            zaclr_eval_stack_destroy(&frame->eval_stack);
            {
                struct zaclr_result result = zaclr_eval_stack_initialize(&frame->eval_stack, frame->max_stack);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = push_object_handle(frame, exception_handle);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
            }

            frame->il_offset = clause->handler_offset;
            return zaclr_result_ok();
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    static struct zaclr_result raise_overflow_exception(struct zaclr_dispatch_context* context,
                                                        struct zaclr_frame* frame)
    {
        zaclr_object_handle exception_handle;
        struct zaclr_result result;

        if (context == NULL || frame == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = create_managed_exception(context->runtime, &exception_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return dispatch_exception_to_handler(context, frame, exception_handle);
    }

    static struct zaclr_result push_binary_i4_checked_result(struct zaclr_dispatch_context* context,
                                                             struct zaclr_frame* frame,
                                                             enum zaclr_opcode opcode,
                                                             int32_t left,
                                                             int32_t right)
    {
        bool overflow = false;
        int64_t result_value = 0;

        switch (opcode)
        {
            case CEE_ADD_OVF:
                overflow = (right > 0 && left > k_int32_max - right) || (right < 0 && left < k_int32_min - right);
                result_value = (int64_t)left + (int64_t)right;
                break;
            case CEE_ADD_OVF_UN:
                overflow = (uint32_t)left > k_uint32_max - (uint32_t)right;
                result_value = (uint32_t)left + (uint32_t)right;
                break;
            case CEE_SUB_OVF:
                overflow = (right < 0 && left > k_int32_max + right) || (right > 0 && left < k_int32_min + right);
                result_value = (int64_t)left - (int64_t)right;
                break;
            case CEE_SUB_OVF_UN:
                overflow = (uint32_t)left < (uint32_t)right;
                result_value = (uint32_t)left - (uint32_t)right;
                break;
            case CEE_MUL_OVF:
                result_value = (int64_t)left * (int64_t)right;
                overflow = result_value > k_int32_max || result_value < k_int32_min;
                break;
            case CEE_MUL_OVF_UN:
            {
                uint64_t unsigned_result = (uint64_t)(uint32_t)left * (uint64_t)(uint32_t)right;
                overflow = unsigned_result > k_uint32_max;
                result_value = (int64_t)unsigned_result;
                break;
            }
            default:
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (overflow)
        {
            return raise_overflow_exception(context, frame);
        }

        return push_i4(frame, (int32_t)result_value);
    }

    static struct zaclr_result push_binary_i8_checked_result(struct zaclr_dispatch_context* context,
                                                             struct zaclr_frame* frame,
                                                             enum zaclr_opcode opcode,
                                                             int64_t left,
                                                             int64_t right)
    {
        bool overflow = false;
        uint64_t unsigned_result;
        int64_t result_value = 0;

        switch (opcode)
        {
            case CEE_ADD_OVF:
                overflow = (right > 0 && left > k_int64_max - right) || (right < 0 && left < k_int64_min - right);
                result_value = left + right;
                break;
            case CEE_ADD_OVF_UN:
                overflow = (uint64_t)left > k_uint64_max - (uint64_t)right;
                result_value = (int64_t)((uint64_t)left + (uint64_t)right);
                break;
            case CEE_SUB_OVF:
                overflow = (right < 0 && left > k_int64_max + right) || (right > 0 && left < k_int64_min + right);
                result_value = left - right;
                break;
            case CEE_SUB_OVF_UN:
                overflow = (uint64_t)left < (uint64_t)right;
                result_value = (int64_t)((uint64_t)left - (uint64_t)right);
                break;
            case CEE_MUL_OVF:
                if (left == 0 || right == 0)
                {
                    result_value = 0;
                }
                else if ((left == -1 && right == k_int64_min) || (right == -1 && left == k_int64_min))
                {
                    overflow = true;
                }
                else
                {
                    bool product_negative = ((left < 0) ^ (right < 0));
                    uint64_t abs_left = left < 0 ? (uint64_t)(-(left + 1)) + 1u : (uint64_t)left;
                    uint64_t abs_right = right < 0 ? (uint64_t)(-(right + 1)) + 1u : (uint64_t)right;
                    uint64_t limit = product_negative ? 0x8000000000000000ull : 0x7FFFFFFFFFFFFFFFull;
                    overflow = abs_left != 0u && abs_right > (limit / abs_left);
                    result_value = left * right;
                }
                break;
            case CEE_MUL_OVF_UN:
                unsigned_result = (uint64_t)left * (uint64_t)right;
                overflow = ((uint64_t)left != 0u) && ((uint64_t)right > (k_uint64_max / (uint64_t)left));
                result_value = (int64_t)unsigned_result;
                break;
            default:
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (overflow)
        {
            return raise_overflow_exception(context, frame);
        }

        return push_i8(frame, result_value);
    }

    static struct zaclr_result push_converted_value(struct zaclr_frame* frame,
                                                    enum zaclr_opcode opcode,
                                                    const struct zaclr_stack_value* value)
    {
        int32_t value_i4;
        int64_t value_i8;
        struct zaclr_result result;

        if (frame == NULL || value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        switch (opcode)
        {
            case CEE_CONV_I1:
                result = stack_value_to_i32(value, &value_i4);
                if (result.status != ZACLR_STATUS_OK) return result;
                return push_i4(frame, (int32_t)(int8_t)value_i4);
            case CEE_CONV_I2:
                result = stack_value_to_i32(value, &value_i4);
                if (result.status != ZACLR_STATUS_OK) return result;
                return push_i4(frame, (int32_t)(int16_t)value_i4);
            case CEE_CONV_I4:
                result = stack_value_to_i32(value, &value_i4);
                if (result.status != ZACLR_STATUS_OK) return result;
                return push_i4(frame, value_i4);
            case CEE_CONV_I:
#if defined(ARCH_X86_64)
                result = stack_value_to_i64(value, &value_i8);
                if (result.status != ZACLR_STATUS_OK) return result;
                return push_i8(frame, value_i8);
#else
                result = stack_value_to_i32(value, &value_i4);
                if (result.status != ZACLR_STATUS_OK) return result;
                return push_i4(frame, value_i4);
#endif
            case CEE_CONV_I8:
                result = stack_value_to_i64(value, &value_i8);
                if (result.status != ZACLR_STATUS_OK) return result;
                return push_i8(frame, value_i8);
            case CEE_CONV_U1:
                result = stack_value_to_i32(value, &value_i4);
                if (result.status != ZACLR_STATUS_OK) return result;
                return push_i4(frame, (int32_t)(uint8_t)value_i4);
            case CEE_CONV_U2:
                result = stack_value_to_i32(value, &value_i4);
                if (result.status != ZACLR_STATUS_OK) return result;
                return push_i4(frame, (int32_t)(uint16_t)value_i4);
            case CEE_CONV_U4:
                result = stack_value_to_i32(value, &value_i4);
                if (result.status != ZACLR_STATUS_OK) return result;
                return push_i4(frame, (int32_t)(uint32_t)value_i4);
            case CEE_CONV_U:
#if defined(ARCH_X86_64)
                result = stack_value_to_i64(value, &value_i8);
                if (result.status != ZACLR_STATUS_OK) return result;
                if (value->kind == ZACLR_STACK_VALUE_I4)
                {
                    return push_i8(frame, (int64_t)(uint64_t)(uint32_t)value_i8);
                }
                return push_i8(frame, (int64_t)(uint64_t)value_i8);
#else
                result = stack_value_to_i32(value, &value_i4);
                if (result.status != ZACLR_STATUS_OK) return result;
                return push_i4(frame, (int32_t)(uint32_t)value_i4);
#endif
            case CEE_CONV_U8:
                result = stack_value_to_i64(value, &value_i8);
                if (result.status != ZACLR_STATUS_OK) return result;
                if (value->kind == ZACLR_STACK_VALUE_I4)
                {
                    return push_i8(frame, (int64_t)(uint64_t)(uint32_t)value_i8);
                }

                return push_i8(frame, (int64_t)(uint64_t)value_i8);
            case CEE_CONV_R4:
                return push_r4(frame, (uint32_t)(stack_value_to_common_float_bits(value) >> (zaclr::emulated_float::k_float64_shift - zaclr::emulated_float::k_float32_shift)));
            case CEE_CONV_R8:
                return push_r8(frame, (uint64_t)stack_value_to_common_float_bits(value));
            case CEE_CONV_R_UN:
                return push_r8(frame, (uint64_t)stack_value_to_common_float_bits_unsigned(value));
            default:
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }
    }

    static struct zaclr_result push_compare_result(struct zaclr_frame* frame,
                                                   enum zaclr_opcode opcode,
                                                   const struct zaclr_stack_value* left,
                                                   const struct zaclr_stack_value* right)
    {
        int32_t result_value = 0;

        if (frame == NULL || left == NULL || right == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (left->kind == ZACLR_STACK_VALUE_I8 || right->kind == ZACLR_STACK_VALUE_I8)
        {
            int64_t left_i8;
            int64_t right_i8;
            struct zaclr_result result = stack_value_to_i64(left, &left_i8);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = stack_value_to_i64(right, &right_i8);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            switch (opcode)
            {
                case CEE_CEQ: result_value = (left_i8 == right_i8) ? 1 : 0; break;
                case CEE_CGT: result_value = (left_i8 > right_i8) ? 1 : 0; break;
                case CEE_CLT: result_value = (left_i8 < right_i8) ? 1 : 0; break;
                case CEE_CGT_UN: result_value = ((uint64_t)left_i8 > (uint64_t)right_i8) ? 1 : 0; break;
                case CEE_CLT_UN: result_value = ((uint64_t)left_i8 < (uint64_t)right_i8) ? 1 : 0; break;
                default:
                    return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            return push_i4(frame, result_value);
        }

        {
            int32_t left_i4;
            int32_t right_i4;
            struct zaclr_result result = stack_value_to_i32(left, &left_i4);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = stack_value_to_i32(right, &right_i4);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            switch (opcode)
            {
                case CEE_CEQ: result_value = (left_i4 == right_i4) ? 1 : 0; break;
                case CEE_CGT: result_value = (left_i4 > right_i4) ? 1 : 0; break;
                case CEE_CLT: result_value = (left_i4 < right_i4) ? 1 : 0; break;
                case CEE_CGT_UN: result_value = ((uint32_t)left_i4 > (uint32_t)right_i4) ? 1 : 0; break;
                case CEE_CLT_UN: result_value = ((uint32_t)left_i4 < (uint32_t)right_i4) ? 1 : 0; break;
                default:
                    return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            return push_i4(frame, result_value);
        }
    }

    static struct zaclr_result push_checked_converted_value(struct zaclr_dispatch_context* context,
                                                            struct zaclr_frame* frame,
                                                            enum zaclr_opcode opcode,
                                                            const struct zaclr_stack_value* value)
    {
        bool is_unsigned_source = false;
        bool produce_i8 = false;
        uint64_t unsigned_value;
        int64_t signed_value;

        if (context == NULL || frame == NULL || value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        switch (opcode)
        {
            case CEE_CONV_OVF_I1_UN:
            case CEE_CONV_OVF_I2_UN:
            case CEE_CONV_OVF_I4_UN:
            case CEE_CONV_OVF_I8_UN:
            case CEE_CONV_OVF_U1_UN:
            case CEE_CONV_OVF_U2_UN:
            case CEE_CONV_OVF_U4_UN:
            case CEE_CONV_OVF_U8_UN:
            case CEE_CONV_OVF_I_UN:
            case CEE_CONV_OVF_U_UN:
                is_unsigned_source = true;
                break;
            default:
                break;
        }

        if (is_unsigned_source)
        {
            if (value->kind == ZACLR_STACK_VALUE_I8)
            {
                unsigned_value = (uint64_t)value->data.i8;
            }
            else if (value->kind == ZACLR_STACK_VALUE_I4)
            {
                unsigned_value = (uint64_t)(uint32_t)value->data.i4;
            }
            else
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }
        }
        else
        {
            struct zaclr_result result = stack_value_to_i64(value, &signed_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            unsigned_value = (uint64_t)signed_value;
        }

        switch (opcode)
        {
            case CEE_CONV_OVF_I1:
            case CEE_CONV_OVF_I1_UN:
                if ((is_unsigned_source && unsigned_value > 127u) || (!is_unsigned_source && (signed_value < -128 || signed_value > 127))) return raise_overflow_exception(context, frame);
                return push_i4(frame, is_unsigned_source ? (int32_t)(int8_t)unsigned_value : (int32_t)(int8_t)signed_value);
            case CEE_CONV_OVF_U1:
            case CEE_CONV_OVF_U1_UN:
                if ((is_unsigned_source && unsigned_value > 255u) || (!is_unsigned_source && (signed_value < 0 || signed_value > 255))) return raise_overflow_exception(context, frame);
                return push_i4(frame, is_unsigned_source ? (int32_t)(uint8_t)unsigned_value : (int32_t)(uint8_t)signed_value);
            case CEE_CONV_OVF_I2:
            case CEE_CONV_OVF_I2_UN:
                if ((is_unsigned_source && unsigned_value > 32767u) || (!is_unsigned_source && (signed_value < -32768 || signed_value > 32767))) return raise_overflow_exception(context, frame);
                return push_i4(frame, is_unsigned_source ? (int32_t)(int16_t)unsigned_value : (int32_t)(int16_t)signed_value);
            case CEE_CONV_OVF_U2:
            case CEE_CONV_OVF_U2_UN:
                if ((is_unsigned_source && unsigned_value > 65535u) || (!is_unsigned_source && (signed_value < 0 || signed_value > 65535))) return raise_overflow_exception(context, frame);
                return push_i4(frame, is_unsigned_source ? (int32_t)(uint16_t)unsigned_value : (int32_t)(uint16_t)signed_value);
            case CEE_CONV_OVF_I4:
            case CEE_CONV_OVF_I4_UN:
                if ((is_unsigned_source && unsigned_value > 0x7FFFFFFFu) || (!is_unsigned_source && (signed_value < k_int32_min || signed_value > k_int32_max))) return raise_overflow_exception(context, frame);
                return push_i4(frame, is_unsigned_source ? (int32_t)(uint32_t)unsigned_value : (int32_t)signed_value);
            case CEE_CONV_OVF_I:
            case CEE_CONV_OVF_I_UN:
#if defined(ARCH_X86_64)
                if (is_unsigned_source)
                {
                    if (unsigned_value > 0x7FFFFFFFFFFFFFFFull) return raise_overflow_exception(context, frame);
                    return push_i8(frame, (int64_t)unsigned_value);
                }

                if (signed_value < k_int64_min || signed_value > k_int64_max) return raise_overflow_exception(context, frame);
                return push_i8(frame, signed_value);
#else
                if ((is_unsigned_source && unsigned_value > 0x7FFFFFFFu) || (!is_unsigned_source && (signed_value < k_int32_min || signed_value > k_int32_max))) return raise_overflow_exception(context, frame);
                return push_i4(frame, is_unsigned_source ? (int32_t)(uint32_t)unsigned_value : (int32_t)signed_value);
#endif
            case CEE_CONV_OVF_U4:
            case CEE_CONV_OVF_U:
            case CEE_CONV_OVF_U4_UN:
            case CEE_CONV_OVF_U_UN:
                if ((is_unsigned_source && unsigned_value > 0xFFFFFFFFu) || (!is_unsigned_source && (signed_value < 0 || signed_value > (int64_t)0xFFFFFFFFu))) return raise_overflow_exception(context, frame);
                return push_i4(frame, is_unsigned_source ? (int32_t)(uint32_t)unsigned_value : (int32_t)(uint32_t)signed_value);
            case CEE_CONV_OVF_I8:
                produce_i8 = true;
                if (signed_value < k_int64_min || signed_value > k_int64_max) return raise_overflow_exception(context, frame);
                break;
            case CEE_CONV_OVF_I8_UN:
                produce_i8 = true;
                if (unsigned_value > 0x7FFFFFFFFFFFFFFFull) return raise_overflow_exception(context, frame);
                return push_i8(frame, (int64_t)unsigned_value);
            case CEE_CONV_OVF_U8:
                produce_i8 = true;
                if (!is_unsigned_source && signed_value < 0) return raise_overflow_exception(context, frame);
                break;
            case CEE_CONV_OVF_U8_UN:
                produce_i8 = true;
                break;
            default:
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (produce_i8)
        {
            return push_i8(frame, is_unsigned_source ? (int64_t)unsigned_value : signed_value);
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    static struct zaclr_result read_array_indexed_value(struct zaclr_frame* frame,
                                                        struct zaclr_array_desc* array,
                                                        uint32_t index,
                                                        enum zaclr_opcode opcode)
    {
        const uint8_t* data;
        struct zaclr_token element_type;
        uint32_t element_size;
        uint32_t length;
        size_t byte_offset;

        if (frame == NULL || array == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        length = zaclr_array_length(array);
        element_size = zaclr_array_element_size(array);
        element_type = zaclr_array_element_type(array);
        data = (const uint8_t*)zaclr_array_data_const(array);
        if (data == NULL || index >= length)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        byte_offset = (size_t)index * element_size;
        switch (opcode)
        {
            case CEE_LDELEM_I1: return push_i4(frame, (int32_t)(int8_t)data[byte_offset]);
            case CEE_LDELEM_U1: return push_i4(frame, (int32_t)data[byte_offset]);
            case CEE_LDELEM_I2: return push_i4(frame, (int32_t)(int16_t)read_u16(data + byte_offset));
            case CEE_LDELEM_U2: return push_i4(frame, (int32_t)read_u16(data + byte_offset));
            case CEE_LDELEM_I4:
            case CEE_LDELEM_U4:
            case CEE_LDELEM_I:
                return push_i4(frame, (int32_t)read_u32(data + byte_offset));
            case CEE_LDELEM_I8:
                return push_i8(frame, (int64_t)read_u64(data + byte_offset));
            case CEE_LDELEM_R4:
                return push_r4(frame, read_u32(data + byte_offset));
            case CEE_LDELEM_R8:
                return push_r8(frame, read_u64(data + byte_offset));
            case CEE_LDELEM_REF:
                return push_object_handle(frame, (zaclr_object_handle)read_u32(data + byte_offset));
            case CEE_LDELEM:
                if (element_size == 1u)
                {
                    return push_i4(frame, (int32_t)data[byte_offset]);
                }

                if (element_size == 2u)
                {
                    return push_i4(frame, (int32_t)read_u16(data + byte_offset));
                }

                if (element_size == 4u)
                {
                    if (token_is_heap_reference(element_type))
                    {
                        return push_object_handle(frame, (zaclr_object_handle)read_u32(data + byte_offset));
                    }

                    return push_i4(frame, (int32_t)read_u32(data + byte_offset));
                }

                if (element_size == 8u)
                {
                    return push_i8(frame, (int64_t)read_u64(data + byte_offset));
                }

                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            default:
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }
    }

    static struct zaclr_result write_array_indexed_value(struct zaclr_array_desc* array,
                                                         uint32_t index,
                                                         enum zaclr_opcode opcode,
                                                         const struct zaclr_stack_value* value)
    {
        uint8_t* data;
        struct zaclr_token element_type;
        uint32_t element_size;
        uint32_t length;
        size_t byte_offset;
        int32_t value_i4;
        int64_t value_i8;
        zaclr_object_handle value_object;
        struct zaclr_result result;

        if (array == NULL || value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        length = zaclr_array_length(array);
        element_size = zaclr_array_element_size(array);
        element_type = zaclr_array_element_type(array);
        data = (uint8_t*)zaclr_array_data(array);
        if (data == NULL || index >= length)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        byte_offset = (size_t)index * element_size;
        switch (opcode)
        {
            case CEE_STELEM_I1:
                result = stack_value_to_i32(value, &value_i4);
                if (result.status != ZACLR_STATUS_OK) return result;
                data[byte_offset] = (uint8_t)value_i4;
                return zaclr_result_ok();
            case CEE_STELEM_I2:
                result = stack_value_to_i32(value, &value_i4);
                if (result.status != ZACLR_STATUS_OK) return result;
                write_u16(data + byte_offset, (uint16_t)value_i4);
                return zaclr_result_ok();
            case CEE_STELEM_I:
            case CEE_STELEM_I4:
                result = stack_value_to_i32(value, &value_i4);
                if (result.status != ZACLR_STATUS_OK) return result;
                write_u32(data + byte_offset, (uint32_t)value_i4);
                return zaclr_result_ok();
            case CEE_STELEM_I8:
                result = stack_value_to_i64(value, &value_i8);
                if (result.status != ZACLR_STATUS_OK) return result;
                write_u64(data + byte_offset, (uint64_t)value_i8);
                return zaclr_result_ok();
            case CEE_STELEM_R4:
                write_u32(data + byte_offset, (uint32_t)(stack_value_to_common_float_bits(value) >> (zaclr::emulated_float::k_float64_shift - zaclr::emulated_float::k_float32_shift)));
                return zaclr_result_ok();
            case CEE_STELEM_R8:
                write_u64(data + byte_offset, (uint64_t)stack_value_to_common_float_bits(value));
                return zaclr_result_ok();
            case CEE_STELEM_REF:
                result = stack_value_to_object_handle(value, &value_object);
                if (result.status != ZACLR_STATUS_OK) return result;
                write_u32(data + byte_offset, value_object);
                return zaclr_result_ok();
            case CEE_STELEM:
                if (element_size == 1u)
                {
                    result = stack_value_to_i32(value, &value_i4);
                    if (result.status != ZACLR_STATUS_OK) return result;
                    data[byte_offset] = (uint8_t)value_i4;
                    return zaclr_result_ok();
                }

                if (element_size == 2u)
                {
                    result = stack_value_to_i32(value, &value_i4);
                    if (result.status != ZACLR_STATUS_OK) return result;
                    write_u16(data + byte_offset, (uint16_t)value_i4);
                    return zaclr_result_ok();
                }

                if (element_size == 4u)
                {
                    if (token_is_heap_reference(element_type))
                    {
                        result = stack_value_to_object_handle(value, &value_object);
                        if (result.status != ZACLR_STATUS_OK) return result;
                        write_u32(data + byte_offset, value_object);
                        return zaclr_result_ok();
                    }

                    result = stack_value_to_i32(value, &value_i4);
                    if (result.status != ZACLR_STATUS_OK) return result;
                    write_u32(data + byte_offset, (uint32_t)value_i4);
                    return zaclr_result_ok();
                }

                if (element_size == 8u)
                {
                    result = stack_value_to_i64(value, &value_i8);
                    if (result.status != ZACLR_STATUS_OK) return result;
                    write_u64(data + byte_offset, (uint64_t)value_i8);
                    return zaclr_result_ok();
                }

                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            default:
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }
    }

    /* Legacy textual internal-call path removed in favor of exact resolved-method binding. */

    static struct zaclr_result invoke_internal_call_exact(struct zaclr_dispatch_context* context,
                                                          struct zaclr_frame* frame,
                                                          const struct zaclr_loaded_assembly* target_assembly,
                                                          const struct zaclr_type_desc* target_type,
                                                          const struct zaclr_method_desc* target_method,
                                                          uint8_t invocation_kind)
    {
        struct zaclr_internal_call_resolution internal_call = {};
        struct zaclr_native_call_frame native_frame = {};
        struct zaclr_result result;
        uint32_t total_arguments;
        uint32_t argument_index;
        struct zaclr_stack_value* stack_arguments = NULL;
        struct zaclr_stack_value* this_value = NULL;
        struct zaclr_stack_value* native_arguments = NULL;
        struct zaclr_stack_value injected_this = {};
        uint8_t has_this;

        if (context == NULL || frame == NULL || target_assembly == NULL || target_type == NULL || target_method == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        ZACLR_TRACE_VALUE(context->runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "ExactBindToken",
                          (uint64_t)target_method->token.raw);
        ZACLR_TRACE_VALUE(context->runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          target_method->name.text,
                          (uint64_t)target_method->impl_flags);

        result = zaclr_internal_call_registry_resolve_exact(&context->runtime->internal_calls,
                                                            target_assembly,
                                                            target_type,
                                                            target_method,
                                                            &internal_call);
        if (result.status != ZACLR_STATUS_OK)
        {
            ZACLR_TRACE_VALUE(context->runtime,
                              ZACLR_TRACE_CATEGORY_INTEROP,
                              ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                              "ExactBindFail",
                              (uint64_t)(((uint32_t)result.category << 16) | (uint32_t)result.status));
            return result;
        }

        ZACLR_TRACE_VALUE(context->runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_NATIVE_CALL,
                          target_method->name.text,
                          (uint64_t)target_method->token.raw);
        ZACLR_TRACE_VALUE(context->runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "Running method",
                          (uint64_t)target_method->token.raw);
        ZACLR_TRACE_VALUE(context->runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "ExactBind.StackDepth",
                          (uint64_t)zaclr_eval_stack_depth(&frame->eval_stack));
        if (zaclr_eval_stack_depth(&frame->eval_stack) != 0u)
        {
            struct zaclr_stack_value peek0 = {};
            if (zaclr_eval_stack_peek(&frame->eval_stack, &peek0).status == ZACLR_STATUS_OK)
            {
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_INTEROP,
                                  ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                  "ExactBind.Peek0Kind",
                                  (uint64_t)peek0.kind);
            }
            if (zaclr_eval_stack_depth(&frame->eval_stack) > 1u)
            {
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_INTEROP,
                                  ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                  "ExactBind.Peek1Kind",
                                  (uint64_t)frame->eval_stack.values[frame->eval_stack.depth - 2u].kind);
            }
            if (zaclr_eval_stack_depth(&frame->eval_stack) > 2u)
            {
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_INTEROP,
                                  ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                  "ExactBind.Peek2Kind",
                                  (uint64_t)frame->eval_stack.values[frame->eval_stack.depth - 3u].kind);
            }
        }

        has_this = (target_method->signature.calling_convention & 0x20u) != 0u ? 1u : 0u;
        total_arguments = target_method->signature.parameter_count + ((has_this != 0u && invocation_kind != ZACLR_NATIVE_CALL_INVOCATION_NEWOBJ) ? 1u : 0u);
        if (total_arguments != 0u)
        {
            stack_arguments = (struct zaclr_stack_value*)kernel_alloc(sizeof(struct zaclr_stack_value) * total_arguments);
            if (stack_arguments == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
            }
        }

        for (argument_index = total_arguments; argument_index > 0u; --argument_index)
        {
            result = zaclr_eval_stack_pop(&frame->eval_stack, &stack_arguments[argument_index - 1u]);
            if (result.status != ZACLR_STATUS_OK)
            {
                if (stack_arguments != NULL)
                {
                    kernel_free(stack_arguments);
                }
                return result;
            }
        }

        if (invocation_kind == ZACLR_NATIVE_CALL_INVOCATION_NEWOBJ && has_this != 0u)
        {
            zaclr_object_handle instance_handle = 0u;
            result = allocate_reference_type_instance(context->runtime,
                                                      target_assembly,
                                                      target_method->owning_type_token,
                                                      &instance_handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                if (stack_arguments != NULL)
                {
                    kernel_free(stack_arguments);
                }
                return result;
            }

            injected_this.kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
            injected_this.data.object_handle = instance_handle;
            this_value = &injected_this;
            native_arguments = stack_arguments;
        }
        else if (has_this != 0u && stack_arguments != NULL)
        {
            ZACLR_TRACE_VALUE(context->runtime,
                              ZACLR_TRACE_CATEGORY_INTEROP,
                              ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                              "ExactBind.ThisKind",
                              (uint64_t)stack_arguments[0].kind);
            this_value = &stack_arguments[0];
            native_arguments = &stack_arguments[1];
        }

        if (target_method->signature.parameter_count != 0u && stack_arguments != NULL)
        {
            ZACLR_TRACE_VALUE(context->runtime,
                              ZACLR_TRACE_CATEGORY_INTEROP,
                              ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                              "ExactBind.Arg0Kind",
                              (uint64_t)stack_arguments[(has_this != 0u && invocation_kind != ZACLR_NATIVE_CALL_INVOCATION_NEWOBJ) ? 1u : 0u].kind);
        }

        if (has_this == 0u)
        {
            native_arguments = stack_arguments;
        }

        result = zaclr_build_native_call_frame(context->runtime,
                                               frame,
                                               target_assembly,
                                               target_method,
                                               invocation_kind,
                                               has_this,
                                               this_value,
                                               (uint8_t)target_method->signature.parameter_count,
                                               native_arguments,
                                               &native_frame);
        if (result.status != ZACLR_STATUS_OK)
        {
            if (stack_arguments != NULL)
            {
                kernel_free(stack_arguments);
            }
            return result;
        }

        result = zaclr_invoke_internal_call(&native_frame, internal_call.method);
        if (stack_arguments != NULL)
        {
            kernel_free(stack_arguments);
        }
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (native_frame.has_result == 0u)
        {
            return zaclr_result_ok();
        }

        return zaclr_eval_stack_push(&frame->eval_stack, &native_frame.result_value);
    }

}

extern "C" struct zaclr_result zaclr_dispatch_step(struct zaclr_dispatch_context* context,
                                                    enum zaclr_opcode opcode)
{
    struct zaclr_frame* frame;
    const struct zaclr_opcode_desc* opcode_desc;

    if (context == NULL || context->runtime == NULL || context->engine == NULL || context->current_frame == NULL || *context->current_frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    frame = *context->current_frame;
    opcode_desc = find_opcode_desc(opcode);
    if (opcode_desc == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (context->runtime->state.config.enable_opcode_trace)
    {
        // Intentionally disabled while debugging conformance failures because per-opcode
        // trace lines were interleaving with [`[FAIL]`](src/managed/Zapada.Conformance/ConformanceTests.cs:74)
        // console output and making the failing test names unreadable in [`build/serial.log`](build/serial.log:1).
        //
        // ZACLR_TRACE_VALUE(context->runtime,
        //                   ZACLR_TRACE_CATEGORY_EXEC,
        //                   ZACLR_TRACE_EVENT_OPCODE_STEP,
        //                   opcode_desc->name,
        //                   (uint64_t)frame->il_offset);
    }

    switch (opcode)
    {
        case CEE_NOP:
            frame->il_offset += 1u;
            return zaclr_result_ok();

        case CEE_LDARG_0:
            frame->il_offset += 1u;
            return load_argument(frame, 0u);

        case CEE_LDARG_1:
            frame->il_offset += 1u;
            return load_argument(frame, 1u);

        case CEE_LDARG_2:
            frame->il_offset += 1u;
            return load_argument(frame, 2u);

        case CEE_LDARG_3:
            frame->il_offset += 1u;
            return load_argument(frame, 3u);

        case CEE_LDARG_S:
        {
            uint32_t argument_index = frame->il_start[frame->il_offset + 1u];
            frame->il_offset += 2u;
            return load_argument(frame, argument_index);
        }

        case CEE_STARG_S:
        {
            uint32_t argument_index = frame->il_start[frame->il_offset + 1u];
            frame->il_offset += 2u;
            return store_argument(frame, argument_index);
        }

        case CEE_LDNULL:
            frame->il_offset += 1u;
            return push_object_handle(frame, 0u);

        case CEE_LDIND_REF:
        {
            struct zaclr_stack_value address_value = {};
            zaclr_object_handle handle = 0u;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &address_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 1u;
            result = load_indirect_ref(frame->runtime, frame, &address_value, &handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            return push_object_handle(frame, handle);
        }

        case CEE_LDSTR:
        {
            uint32_t token = read_u32(frame->il_start + frame->il_offset + 1u);
            zaclr_object_handle string_handle;
            struct zaclr_result result = metadata_get_user_string_handle(&frame->runtime->heap,
                                                                         &frame->assembly->metadata,
                                                                         token & 0x00FFFFFFu,
                                                                         &string_handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 5u;
            return push_object_handle(frame, string_handle);
        }

        case CEE_CALL:
        case CEE_CALLVIRT:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            struct zaclr_token resolved_token = token;
            ZACLR_TRACE_VALUE(context->runtime,
                              ZACLR_TRACE_CATEGORY_EXEC,
                              ZACLR_TRACE_EVENT_CALL_TARGET,
                              "CallTokenRaw",
                              (uint64_t)token.raw);
            frame->il_offset += 5u;

            if (zaclr_token_matches_table(&resolved_token, ZACLR_TOKEN_TABLE_METHODSPEC))
            {
                struct zaclr_methodspec_row methodspec_row = {};
                struct zaclr_slice instantiation_blob = {};
                struct zaclr_generic_instantiation_desc instantiation = {};
                struct zaclr_result result;
                uint32_t method_tag;
                uint32_t method_row;

                result = zaclr_metadata_reader_get_methodspec_row(&frame->assembly->metadata,
                                                                  zaclr_token_row(&resolved_token),
                                                                  &methodspec_row);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = zaclr_metadata_reader_get_blob(&frame->assembly->metadata,
                                                        methodspec_row.instantiation_blob_index,
                                                        &instantiation_blob);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = zaclr_signature_parse_generic_instantiation(&instantiation_blob, &instantiation);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
                }

                method_tag = methodspec_row.method_coded_index & 0x1u;
                method_row = methodspec_row.method_coded_index >> 1u;
                if (method_row == 0u)
                {
                    return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
                }

                resolved_token = zaclr_token_make(((uint32_t)(method_tag == 0u ? ZACLR_TOKEN_TABLE_METHOD : ZACLR_TOKEN_TABLE_MEMBERREF) << 24) | method_row);
            }

            if (zaclr_token_matches_table(&resolved_token, ZACLR_TOKEN_TABLE_METHOD))
            {
                const struct zaclr_method_desc* method;
                struct zaclr_frame* child;
                struct zaclr_result result;

                method = zaclr_method_map_find_by_token(&frame->assembly->method_map, resolved_token);
                if (method == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
                }

                /*
                 * Exact native binding still runs on resolved bodyless methods.
                 * Do not require impl_flags here as the primary gate; the resolved
                 * method body state plus exact runtime bind decides the outcome.
                 */
                if (method->rva == 0u)
                {
                    const struct zaclr_type_desc* owning_type = zaclr_type_map_find_by_token(&frame->assembly->type_map,
                                                                                              method->owning_type_token);
                    ZACLR_TRACE_VALUE(context->runtime,
                                      ZACLR_TRACE_CATEGORY_INTEROP,
                                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                      "BodylessMethodGate",
                                      (uint64_t)method->token.raw);
                    ZACLR_TRACE_VALUE(context->runtime,
                                      ZACLR_TRACE_CATEGORY_INTEROP,
                                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                      method->name.text,
                                      (uint64_t)method->impl_flags);
                    if (owning_type == NULL)
                    {
                        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
                    }

                    return invoke_internal_call_exact(context,
                                                      frame,
                                                      frame->assembly,
                                                      owning_type,
                                                      method,
                                                      ZACLR_NATIVE_CALL_INVOCATION_CALL);
                }

                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  method->name.text,
                                  (uint64_t)method->token.raw);

                        result = zaclr_frame_create_child(context->engine,
                                                          context->runtime,
                                                          frame,
                                                          frame->assembly,
                                                          method,
                                                          &child);
                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_EXEC,
                                          ZACLR_TRACE_EVENT_CALL_TARGET,
                                          "Call.MethodDef.CreateChildStatus",
                                          (uint64_t)result.status);
                        if (result.status != ZACLR_STATUS_OK)
                        {
                            return result;
                        }

                        result = zaclr_frame_bind_arguments(child, &frame->eval_stack);
                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_EXEC,
                                          ZACLR_TRACE_EVENT_CALL_TARGET,
                                          "Call.MethodDef.BindStatus",
                                          (uint64_t)result.status);
                        if (result.status != ZACLR_STATUS_OK)
                        {
                            zaclr_frame_destroy(child);
                            return result;
                        }

                *context->current_frame = child;
                return zaclr_result_ok();
            }

            if (zaclr_token_matches_table(&resolved_token, ZACLR_TOKEN_TABLE_MEMBERREF))
            {
                struct zaclr_memberref_target memberref = {};
                return zaclr_metadata_get_memberref_info(frame->assembly, resolved_token, &memberref).status == ZACLR_STATUS_OK
                    ? ([&]() -> struct zaclr_result {
                        const struct zaclr_loaded_assembly* target_assembly;
                        const struct zaclr_type_desc* target_type;
                        const struct zaclr_method_desc* target_method;
                        struct zaclr_frame* child;
                        struct zaclr_result result;

                        if (is_system_object_ctor(&memberref))
                        {
                            return zaclr_result_ok();
                        }

                        result = zaclr_member_resolution_resolve_method(context->runtime,
                                                                       frame->assembly,
                                                                       &memberref,
                                                                       &target_assembly,
                                                                       &target_type,
                                                                       &target_method);
                        if (result.status != ZACLR_STATUS_OK)
                        {
                            return result;
                        }

                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_EXEC,
                                          ZACLR_TRACE_EVENT_CALL_TARGET,
                                          target_assembly->assembly_name.text,
                                          (uint64_t)target_assembly->id);

                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_INTEROP,
                                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                          "ResolvedMemberRefMethod",
                                          (uint64_t)target_method->token.raw);
                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_INTEROP,
                                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                          target_method->name.text,
                                          (uint64_t)target_method->impl_flags);

                        /* Same rule for resolved cross-assembly bodyless methods. */
                        if (target_method->rva == 0u)
                        {
                            return invoke_internal_call_exact(context,
                                                              frame,
                                                              target_assembly,
                                                              target_type,
                                                              target_method,
                                                              ZACLR_NATIVE_CALL_INVOCATION_CALL);
                        }

                        result = zaclr_frame_create_child(context->engine,
                                                          context->runtime,
                                                          frame,
                                                          target_assembly,
                                                          target_method,
                                                          &child);
                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_EXEC,
                                          ZACLR_TRACE_EVENT_CALL_TARGET,
                                          "Call.MemberRef.CreateChildStatus",
                                          (uint64_t)result.status);
                        if (result.status != ZACLR_STATUS_OK)
                        {
                            return result;
                        }

                        result = zaclr_frame_bind_arguments(child, &frame->eval_stack);
                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_EXEC,
                                          ZACLR_TRACE_EVENT_CALL_TARGET,
                                          "Call.MemberRef.BindStatus",
                                          (uint64_t)result.status);
                        if (result.status != ZACLR_STATUS_OK)
                        {
                            zaclr_frame_destroy(child);
                            return result;
                        }

                        *context->current_frame = child;
                        return zaclr_result_ok();
                    })()
                    : zaclr_metadata_get_memberref_info(frame->assembly, token, &memberref);
            }

            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        case CEE_NEWOBJ:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            ZACLR_TRACE_VALUE(context->runtime,
                              ZACLR_TRACE_CATEGORY_EXEC,
                              ZACLR_TRACE_EVENT_CALL_TARGET,
                              "NewObj.Token",
                              (uint64_t)token.raw);
            frame->il_offset += 5u;

            if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_METHOD))
            {
                const struct zaclr_method_desc* method = zaclr_method_map_find_by_token(&frame->assembly->method_map, token);
                struct zaclr_frame* child;
                zaclr_object_handle instance_handle;
                struct zaclr_result result;

                if (method == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
                }

                result = allocate_reference_type_instance(frame->runtime,
                                                          frame->assembly,
                                                          method->owning_type_token,
                                                          &instance_handle);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "NewObj.Method.AllocateStatus",
                                  (uint64_t)result.status);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = zaclr_frame_create_child(context->engine,
                                                  context->runtime,
                                                  frame,
                                                  frame->assembly,
                                                  method,
                                                  &child);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "NewObj.Method.CreateChildStatus",
                                  (uint64_t)result.status);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = bind_newobj_arguments(child, &frame->eval_stack, instance_handle);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "NewObj.Method.BindStatus",
                                  (uint64_t)result.status);
                if (result.status != ZACLR_STATUS_OK)
                {
                    zaclr_frame_destroy(child);
                    return result;
                }

                *context->current_frame = child;
                return zaclr_result_ok();
            }

            if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_MEMBERREF))
            {
                struct zaclr_memberref_target memberref = {};
                struct zaclr_result result = zaclr_metadata_get_memberref_info(frame->assembly, token, &memberref);
                struct zaclr_frame* child = NULL;
                zaclr_object_handle instance_handle = 0u;
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_INTEROP,
                                  ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                  "NewObj.MemberRefAssembly",
                                  0u);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_INTEROP,
                                  ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                  memberref.assembly_name,
                                  0u);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_INTEROP,
                                  ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                  memberref.key.type_name,
                                  0u);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_INTEROP,
                                  ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                  memberref.key.method_name,
                                  (uint64_t)memberref.signature.parameter_count);

                const struct zaclr_loaded_assembly* target_assembly = NULL;
                const struct zaclr_type_desc* target_type = NULL;
                const struct zaclr_method_desc* target_method = NULL;

            result = zaclr_member_resolution_resolve_method(frame->runtime,
                                                            frame->assembly,
                                                            &memberref,
                                                            &target_assembly,
                                                            &target_type,
                                                            &target_method);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "NewObj.TargetAssembly",
                                  target_assembly != NULL ? (uint64_t)target_assembly->id : 0u);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  target_assembly != NULL ? target_assembly->assembly_name.text : "<null>",
                                  target_method != NULL ? (uint64_t)target_method->token.raw : 0u);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  target_type != NULL ? target_type->type_name.text : "<null>",
                                  target_method != NULL ? (uint64_t)target_method->rva : 0u);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  target_method != NULL ? target_method->name.text : "<null>",
                                  target_method != NULL ? (uint64_t)target_method->impl_flags : 0u);

                if ((target_method->impl_flags & k_method_impl_flag_internal_call) != 0u)
                {
                    return invoke_internal_call_exact(context,
                                                      frame,
                                                      target_assembly,
                                                      target_type,
                                                      target_method,
                                                      ZACLR_NATIVE_CALL_INVOCATION_NEWOBJ);
                }

                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "NewObj.BindArgumentCount",
                                  (uint64_t)memberref.signature.parameter_count);

                result = allocate_reference_type_instance(frame->runtime,
                                                          target_assembly,
                                                          target_method->owning_type_token,
                                                          &instance_handle);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "NewObj.MemberRef.AllocateStatus",
                                  (uint64_t)result.status);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "NewObj.InstanceHandle",
                                  (uint64_t)instance_handle);

                result = zaclr_frame_create_child(context->engine,
                                                  context->runtime,
                                                  frame,
                                                  target_assembly,
                                                  target_method,
                                                  &child);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "NewObj.MemberRef.CreateChildStatus",
                                  (uint64_t)result.status);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "NewObj.ChildFrame",
                                  child != NULL ? (uint64_t)child->id : 0u);

                result = bind_newobj_arguments(child, &frame->eval_stack, instance_handle);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "NewObj.MemberRef.BindStatus",
                                  (uint64_t)result.status);
                if (result.status != ZACLR_STATUS_OK)
                {
                    zaclr_frame_destroy(child);
                    return result;
                }

                *context->current_frame = child;
                return zaclr_result_ok();
            }

            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        case CEE_BOX:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            struct zaclr_stack_value value;
            zaclr_object_handle handle;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 5u;
            result = box_stack_value(frame->runtime, &value, token, &handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            return push_object_handle(frame, handle);
        }

        case CEE_UNBOX_ANY:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            struct zaclr_stack_value value;
            zaclr_object_handle handle;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 5u;
            result = stack_value_to_object_handle(&value, &handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = unbox_any_value(frame->runtime, handle, token, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            return zaclr_eval_stack_push(&frame->eval_stack, &value);
        }

        case CEE_ISINST:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            struct zaclr_stack_value value;
            zaclr_object_handle handle;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 5u;
            result = stack_value_to_object_handle(&value, &handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            return push_object_handle(frame,
                                      object_matches_type(frame->runtime, frame->assembly, handle, token)
                                          ? handle
                                          : 0u);
        }

        case CEE_CASTCLASS:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            struct zaclr_stack_value value;
            zaclr_object_handle handle;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 5u;
            result = stack_value_to_object_handle(&value, &handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (handle != 0u && !object_matches_type(frame->runtime, frame->assembly, handle, token))
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            return push_object_handle(frame, handle);
        }

        case CEE_INITOBJ:
        {
            struct zaclr_stack_value address_value;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &address_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (address_value.kind != ZACLR_STACK_VALUE_LOCAL_ADDRESS || address_value.data.raw >= frame->local_count)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            frame->il_offset += 6u;
            zaclr_object_handle handle = 0u;
            result = zaclr_reference_object_allocate(&frame->runtime->heap,
                                                     NULL,
                                                     0u,
                                                     zaclr_token_make(0u),
                                                     0u,
                                                     &handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->locals[address_value.data.raw].kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
            frame->locals[address_value.data.raw].data.object_handle = handle;
            return zaclr_result_ok();
        }

        case CEE_STLOC_0:
            frame->il_offset += 1u;
            return store_local(frame, 0u);

        case CEE_STLOC_1:
            frame->il_offset += 1u;
            return store_local(frame, 1u);

        case CEE_STLOC_2:
            frame->il_offset += 1u;
            return store_local(frame, 2u);

        case CEE_STLOC_3:
            frame->il_offset += 1u;
            return store_local(frame, 3u);

        case CEE_STLOC_S:
        {
            uint32_t local_index = frame->il_start[frame->il_offset + 1u];
            frame->il_offset += 2u;
            return store_local(frame, local_index);
        }

        case CEE_STSFLD:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            frame->il_offset += 5u;
            return store_static_field(frame, token);
        }

        case CEE_STFLD:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            struct zaclr_memberref_target memberref = {};
            struct zaclr_stack_value field_value;
            struct zaclr_stack_value object_value;
            zaclr_object_handle handle;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &field_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_eval_stack_pop(&frame->eval_stack, &object_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (object_value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
            {
                struct zaclr_stack_value* target = resolve_local_address_target(frame, &object_value);
                if (target == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }

                object_value = *target;
            }

            result = stack_value_to_object_handle(&object_value, &handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_MEMBERREF))
            {
                const struct zaclr_loaded_assembly* target_assembly = NULL;
                uint32_t target_field_row = 0u;

                result = zaclr_metadata_get_memberref_info(frame->assembly, token, &memberref);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = zaclr_member_resolution_resolve_field(frame->runtime,
                                                               &memberref,
                                                               &target_assembly,
                                                               &target_field_row);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | target_field_row);
            }

            frame->il_offset += 5u;
            return store_object_field(frame->runtime, handle, token, &field_value);
        }

        case CEE_LDFLD:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            struct zaclr_memberref_target memberref = {};
            struct zaclr_stack_value object_value;
            struct zaclr_stack_value field_value;
            zaclr_object_handle handle;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &object_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            ZACLR_TRACE_VALUE(context->runtime,
                              ZACLR_TRACE_CATEGORY_EXEC,
                              ZACLR_TRACE_EVENT_CALL_TARGET,
                              "Ldfld.ObjectKind",
                              (uint64_t)object_value.kind);
            ZACLR_TRACE_VALUE(context->runtime,
                              ZACLR_TRACE_CATEGORY_EXEC,
                              ZACLR_TRACE_EVENT_CALL_TARGET,
                              "Ldfld.Token",
                              (uint64_t)token.raw);

            if (object_value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
            {
                struct zaclr_stack_value* target = resolve_local_address_target(frame, &object_value);
                if (target == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }

                object_value = *target;
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "Ldfld.ResolvedLocalKind",
                                  (uint64_t)object_value.kind);
            }

            result = stack_value_to_object_handle(&object_value, &handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (frame->method != NULL
                && frame->method->name.text != NULL
                && text_equals(frame->method->name.text, "get_Length"))
            {
                zaclr_object_handle string_handle = handle;
                if (frame->argument_count != 0u
                    && frame->arguments != NULL
                    && frame->arguments[0].kind == ZACLR_STACK_VALUE_OBJECT_HANDLE
                    && frame->arguments[0].data.object_handle != 0u)
                {
                    string_handle = frame->arguments[0].data.object_handle;
                }

                const struct zaclr_string_desc* string_object = zaclr_string_from_handle_const(&frame->runtime->heap, string_handle);
                if (string_object != NULL)
                {
                    frame->il_offset += 5u;
                    return push_i4(frame, (int32_t)zaclr_string_length(string_object));
                }
            }

            if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_MEMBERREF))
            {
                const struct zaclr_loaded_assembly* target_assembly = NULL;
                uint32_t target_field_row = 0u;

                result = zaclr_metadata_get_memberref_info(frame->assembly, token, &memberref);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = zaclr_member_resolution_resolve_field(frame->runtime,
                                                               &memberref,
                                                               &target_assembly,
                                                               &target_field_row);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | target_field_row);
            }

            frame->il_offset += 5u;
            result = load_object_field(frame->runtime, handle, token, &field_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            return zaclr_eval_stack_push(&frame->eval_stack, &field_value);
        }

        case CEE_LDFLDA:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            struct zaclr_memberref_target memberref = {};
            struct zaclr_stack_value object_value = {};
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &object_value);
            zaclr_object_handle handle;

            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (object_value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
            {
                struct zaclr_stack_value* target = resolve_local_address_target(frame, &object_value);
                if (target == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }

                object_value = *target;
            }

            result = stack_value_to_object_handle(&object_value, &handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_MEMBERREF))
            {
                const struct zaclr_loaded_assembly* target_assembly = NULL;
                uint32_t target_field_row = 0u;

                result = zaclr_metadata_get_memberref_info(frame->assembly, token, &memberref);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = zaclr_member_resolution_resolve_field(frame->runtime,
                                                               &memberref,
                                                               &target_assembly,
                                                               &target_field_row);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | target_field_row);
            }

            {
                struct zaclr_reference_object_desc* object = zaclr_reference_object_from_handle(&frame->runtime->heap, handle);
                struct zaclr_stack_value* field_storage;
                if (object == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
                }

                field_storage = zaclr_reference_object_field_storage(object, token);
                if (field_storage == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
                }

                frame->il_offset += 5u;
                {
                    struct zaclr_stack_value address_value = {};
                    address_value.kind = ZACLR_STACK_VALUE_LOCAL_ADDRESS;
                    address_value.data.raw = (uintptr_t)field_storage;
                    return zaclr_eval_stack_push(&frame->eval_stack, &address_value);
                }
            }
        }

        case CEE_LDLOC_0:
            frame->il_offset += 1u;
            return load_local(frame, 0u);

        case CEE_LDLOC_1:
            frame->il_offset += 1u;
            return load_local(frame, 1u);

        case CEE_LDLOC_2:
            frame->il_offset += 1u;
            return load_local(frame, 2u);

        case CEE_LDLOC_3:
            frame->il_offset += 1u;
            return load_local(frame, 3u);

        case CEE_LDLOC_S:
        {
            uint32_t local_index = frame->il_start[frame->il_offset + 1u];
            frame->il_offset += 2u;
            return load_local(frame, local_index);
        }

        case CEE_LDLOCA_S:
        {
            uint32_t local_index = frame->il_start[frame->il_offset + 1u];
            frame->il_offset += 2u;
            return push_local_address(frame, local_index);
        }

        case CEE_LDLOC:
        {
            uint32_t local_index = read_u16(frame->il_start + frame->il_offset + 2u);
            frame->il_offset += 4u;
            return load_local(frame, local_index);
        }

        case CEE_LDSFLD:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            frame->il_offset += 5u;
            return load_static_field(frame, token);
        }

        case CEE_LDC_I4_M1:
            frame->il_offset += 1u;
            return push_i4(frame, -1);

        case CEE_LDC_I4_0:
            frame->il_offset += 1u;
            return push_i4(frame, 0);

        case CEE_LDC_I4_1:
            frame->il_offset += 1u;
            return push_i4(frame, 1);

        case CEE_LDC_I4_2:
            frame->il_offset += 1u;
            return push_i4(frame, 2);

        case CEE_LDC_I4_3:
            frame->il_offset += 1u;
            return push_i4(frame, 3);

        case CEE_LDC_I4_4:
            frame->il_offset += 1u;
            return push_i4(frame, 4);

        case CEE_LDC_I4_5:
            frame->il_offset += 1u;
            return push_i4(frame, 5);

        case CEE_LDC_I4_6:
            frame->il_offset += 1u;
            return push_i4(frame, 6);

        case CEE_LDC_I4_7:
            frame->il_offset += 1u;
            return push_i4(frame, 7);

        case CEE_LDC_I4_8:
            frame->il_offset += 1u;
            return push_i4(frame, 8);

        case CEE_LDC_I4_S:
        {
            int8_t value = (int8_t)frame->il_start[frame->il_offset + 1u];
            frame->il_offset += 2u;
            return push_i4(frame, (int32_t)value);
        }

        case CEE_LDC_I4:
        {
            int32_t value = (int32_t)read_u32(frame->il_start + frame->il_offset + 1u);
            frame->il_offset += 5u;
            return push_i4(frame, value);
        }

        case CEE_LDC_I8:
        {
            int64_t value = (int64_t)read_u64(frame->il_start + frame->il_offset + 1u);
            frame->il_offset += 9u;
            return push_i8(frame, value);
        }

        case CEE_LDC_R4:
        {
            uint32_t value = read_u32(frame->il_start + frame->il_offset + 1u);
            frame->il_offset += 5u;
#if defined(PLATFORM_EMULATED_FLOATINGPOINT)
            return push_r4(frame, (uint32_t)(zaclr::emulated_float::decode_r4_to_common(value) >> (zaclr::emulated_float::k_float64_shift - zaclr::emulated_float::k_float32_shift)));
#else
            return push_r4(frame, value);
#endif
        }

        case CEE_LDC_R8:
        {
            uint64_t value = read_u64(frame->il_start + frame->il_offset + 1u);
            frame->il_offset += 9u;
#if defined(PLATFORM_EMULATED_FLOATINGPOINT)
            return push_r8(frame, (uint64_t)zaclr::emulated_float::decode_r8_to_common(value));
#else
            return push_r8(frame, value);
#endif
        }

        case CEE_DUP:
        {
            struct zaclr_stack_value value;
            struct zaclr_result result = zaclr_eval_stack_peek(&frame->eval_stack, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 1u;
            return zaclr_eval_stack_push(&frame->eval_stack, &value);
        }

        case CEE_POP:
        {
            struct zaclr_stack_value value;
            frame->il_offset += 1u;
            return zaclr_eval_stack_pop(&frame->eval_stack, &value);
        }

        case CEE_NEWARR:
        {
            struct zaclr_stack_value count_value;
            struct zaclr_result result;
            uint32_t length;
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            uint32_t element_size;
            zaclr_object_handle array_handle;

            result = zaclr_eval_stack_pop(&frame->eval_stack, &count_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = stack_value_to_u32(&count_value, &length);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            element_size = element_size_for_token(frame->assembly, token);
            result = zaclr_array_allocate(&frame->runtime->heap,
                                          0u,
                                          token,
                                          element_size,
                                          length,
                                          &array_handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 5u;
            return push_object_handle(frame, array_handle);
        }

        case CEE_LDLEN:
        {
            struct zaclr_stack_value array_value;
            struct zaclr_array_desc* array;
            zaclr_object_handle array_handle;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &array_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = stack_value_to_object_handle(&array_value, &array_handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            array = zaclr_array_from_handle(&frame->runtime->heap, array_handle);
            if (array == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
            }

            frame->il_offset += 1u;
            return push_i4(frame, (int32_t)zaclr_array_length(array));
        }

        case CEE_LDELEM_I1:
        case CEE_LDELEM_U1:
        case CEE_LDELEM_I2:
        case CEE_LDELEM_U2:
        case CEE_LDELEM_I4:
        case CEE_LDELEM_U4:
        case CEE_LDELEM_I8:
        case CEE_LDELEM_I:
        case CEE_LDELEM_R4:
        case CEE_LDELEM_R8:
        case CEE_LDELEM_REF:
        case CEE_LDELEM:
        {
            struct zaclr_stack_value index_value;
            struct zaclr_stack_value array_value;
            struct zaclr_array_desc* array;
            struct zaclr_result result;
            uint32_t index;
            zaclr_object_handle array_handle;

            result = zaclr_eval_stack_pop(&frame->eval_stack, &index_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_eval_stack_pop(&frame->eval_stack, &array_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = stack_value_to_u32(&index_value, &index);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = stack_value_to_object_handle(&array_value, &array_handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            array = zaclr_array_from_handle(&frame->runtime->heap, array_handle);
            if (array == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
            }

            frame->il_offset += opcode == CEE_LDELEM ? 5u : 1u;
            return read_array_indexed_value(frame, array, index, opcode);
        }

        case CEE_STELEM_I:
        case CEE_STELEM_I1:
        case CEE_STELEM_I2:
        case CEE_STELEM_I4:
        case CEE_STELEM_I8:
        case CEE_STELEM_R4:
        case CEE_STELEM_R8:
        case CEE_STELEM_REF:
        case CEE_STELEM:
        {
            struct zaclr_stack_value element_value;
            struct zaclr_stack_value index_value;
            struct zaclr_stack_value array_value;
            struct zaclr_array_desc* array;
            struct zaclr_result result;
            uint32_t index;
            zaclr_object_handle array_handle;

            result = zaclr_eval_stack_pop(&frame->eval_stack, &element_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_eval_stack_pop(&frame->eval_stack, &index_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_eval_stack_pop(&frame->eval_stack, &array_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = stack_value_to_u32(&index_value, &index);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = stack_value_to_object_handle(&array_value, &array_handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            array = zaclr_array_from_handle(&frame->runtime->heap, array_handle);
            if (array == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
            }

            frame->il_offset += opcode == CEE_STELEM ? 5u : 1u;
            return write_array_indexed_value(array, index, opcode, &element_value);
        }

        case CEE_BR_S:
        {
            int8_t delta = (int8_t)frame->il_start[frame->il_offset + 1u];
            frame->il_offset = compute_short_branch_target(frame->il_offset, delta);
            return zaclr_result_ok();
        }

        case CEE_BR:
        {
            int32_t delta = (int32_t)read_u32(frame->il_start + frame->il_offset + 1u);
            frame->il_offset = compute_inline_branch_target(frame->il_offset, delta);
            return zaclr_result_ok();
        }

        case CEE_BRFALSE_S:
        case CEE_BRTRUE_S:
        {
            int8_t delta = (int8_t)frame->il_start[frame->il_offset + 1u];
            struct zaclr_stack_value value;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            bool branch;

            if (result.status != ZACLR_STATUS_OK)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            if (value.kind == ZACLR_STACK_VALUE_I4)
            {
                branch = (value.data.i4 != 0);
            }
            else if (value.kind == ZACLR_STACK_VALUE_I8)
            {
                branch = (value.data.i8 != 0);
            }
            else if (value.kind == ZACLR_STACK_VALUE_OBJECT_HANDLE)
            {
                branch = (value.data.object_handle != 0u);
            }
            else
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            if (opcode == CEE_BRFALSE_S)
            {
                branch = !branch;
            }

            frame->il_offset = branch
                ? compute_short_branch_target(frame->il_offset, delta)
                : (frame->il_offset + 2u);
            return zaclr_result_ok();
        }

        case CEE_BRFALSE:
        case CEE_BRTRUE:
        {
            int32_t delta = (int32_t)read_u32(frame->il_start + frame->il_offset + 1u);
            struct zaclr_stack_value value;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            bool branch;

            if (result.status != ZACLR_STATUS_OK)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            if (value.kind == ZACLR_STACK_VALUE_I4)
            {
                branch = (value.data.i4 != 0);
            }
            else if (value.kind == ZACLR_STACK_VALUE_I8)
            {
                branch = (value.data.i8 != 0);
            }
            else if (value.kind == ZACLR_STACK_VALUE_OBJECT_HANDLE)
            {
                branch = (value.data.object_handle != 0u);
            }
            else
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            if (opcode == CEE_BRFALSE)
            {
                branch = !branch;
            }

            frame->il_offset = branch
                ? compute_inline_branch_target(frame->il_offset, delta)
                : (frame->il_offset + 5u);
            return zaclr_result_ok();
        }

        case CEE_BEQ_S:
        case CEE_BNE_UN_S:
        case CEE_BGE_S:
        case CEE_BGT_S:
        case CEE_BLE_S:
        case CEE_BLT_S:
        case CEE_BGE_UN_S:
        case CEE_BGT_UN_S:
        case CEE_BLE_UN_S:
        case CEE_BLT_UN_S:
        {
            int8_t delta = (int8_t)frame->il_start[frame->il_offset + 1u];
            bool branch = false;

            if (opcode == CEE_BEQ_S || opcode == CEE_BNE_UN_S || opcode == CEE_BGE_S || opcode == CEE_BGT_S || opcode == CEE_BLE_S || opcode == CEE_BLT_S)
            {
                struct zaclr_stack_value peek_right;
                struct zaclr_result result = zaclr_eval_stack_peek(&frame->eval_stack, &peek_right);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                if (stack_value_is_float(&peek_right))
                {
                    int64_t left;
                    int64_t right;
                    result = pop_branch_float_pair(frame, &left, &right);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }

                    switch (opcode)
                    {
                        case CEE_BEQ_S: branch = (left == right); break;
                        case CEE_BNE_UN_S: branch = (left != right); break;
                        case CEE_BGE_S: branch = (left >= right); break;
                        case CEE_BGT_S: branch = (left > right); break;
                        case CEE_BLE_S: branch = (left <= right); break;
                        case CEE_BLT_S: branch = (left < right); break;
                        default: break;
                    }
                }
                else
                {
                    int32_t left;
                    int32_t right;
                    result = pop_branch_i4_pair(frame, &left, &right);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }

                    switch (opcode)
                    {
                        case CEE_BEQ_S: branch = (left == right); break;
                        case CEE_BNE_UN_S: branch = (left != right); break;
                        case CEE_BGE_S: branch = (left >= right); break;
                        case CEE_BGT_S: branch = (left > right); break;
                        case CEE_BLE_S: branch = (left <= right); break;
                        case CEE_BLT_S: branch = (left < right); break;
                        default: break;
                    }
                }
            }
            else
            {
                struct zaclr_stack_value peek_right;
                struct zaclr_result result = zaclr_eval_stack_peek(&frame->eval_stack, &peek_right);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                if (stack_value_is_float(&peek_right))
                {
                    int64_t left;
                    int64_t right;
                    result = pop_branch_float_pair(frame, &left, &right);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }

                    switch (opcode)
                    {
                        case CEE_BGE_UN_S: branch = (left >= right); break;
                        case CEE_BGT_UN_S: branch = (left > right); break;
                        case CEE_BLE_UN_S: branch = (left <= right); break;
                        case CEE_BLT_UN_S: branch = (left < right); break;
                        default: break;
                    }
                }
                else
                {
                    uint32_t left;
                    uint32_t right;
                    result = pop_branch_u4_pair(frame, &left, &right);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }

                    switch (opcode)
                    {
                        case CEE_BGE_UN_S: branch = (left >= right); break;
                        case CEE_BGT_UN_S: branch = (left > right); break;
                        case CEE_BLE_UN_S: branch = (left <= right); break;
                        case CEE_BLT_UN_S: branch = (left < right); break;
                        default: break;
                    }
                }
            }

            frame->il_offset = branch
                ? compute_short_branch_target(frame->il_offset, delta)
                : (frame->il_offset + 2u);
            return zaclr_result_ok();
        }

        case CEE_BEQ:
        case CEE_BNE_UN:
        case CEE_BGE:
        case CEE_BGT:
        case CEE_BLE:
        case CEE_BLT:
        case CEE_BGE_UN:
        case CEE_BGT_UN:
        case CEE_BLE_UN:
        case CEE_BLT_UN:
        {
            int32_t delta = (int32_t)read_u32(frame->il_start + frame->il_offset + 1u);
            bool branch = false;

            if (opcode == CEE_BEQ || opcode == CEE_BNE_UN || opcode == CEE_BGE || opcode == CEE_BGT || opcode == CEE_BLE || opcode == CEE_BLT)
            {
                struct zaclr_stack_value peek_right;
                struct zaclr_result result = zaclr_eval_stack_peek(&frame->eval_stack, &peek_right);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                if (stack_value_is_float(&peek_right))
                {
                    int64_t left;
                    int64_t right;
                    result = pop_branch_float_pair(frame, &left, &right);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }

                    switch (opcode)
                    {
                        case CEE_BEQ: branch = (left == right); break;
                        case CEE_BNE_UN: branch = (left != right); break;
                        case CEE_BGE: branch = (left >= right); break;
                        case CEE_BGT: branch = (left > right); break;
                        case CEE_BLE: branch = (left <= right); break;
                        case CEE_BLT: branch = (left < right); break;
                        default: break;
                    }
                }
                else
                {
                    int32_t left;
                    int32_t right;
                    result = pop_branch_i4_pair(frame, &left, &right);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }

                    switch (opcode)
                    {
                        case CEE_BEQ: branch = (left == right); break;
                        case CEE_BNE_UN: branch = (left != right); break;
                        case CEE_BGE: branch = (left >= right); break;
                        case CEE_BGT: branch = (left > right); break;
                        case CEE_BLE: branch = (left <= right); break;
                        case CEE_BLT: branch = (left < right); break;
                        default: break;
                    }
                }
            }
            else
            {
                struct zaclr_stack_value peek_right;
                struct zaclr_result result = zaclr_eval_stack_peek(&frame->eval_stack, &peek_right);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                if (stack_value_is_float(&peek_right))
                {
                    int64_t left;
                    int64_t right;
                    result = pop_branch_float_pair(frame, &left, &right);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }

                    switch (opcode)
                    {
                        case CEE_BGE_UN: branch = (left >= right); break;
                        case CEE_BGT_UN: branch = (left > right); break;
                        case CEE_BLE_UN: branch = (left <= right); break;
                        case CEE_BLT_UN: branch = (left < right); break;
                        default: break;
                    }
                }
                else
                {
                    uint32_t left;
                    uint32_t right;
                    result = pop_branch_u4_pair(frame, &left, &right);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }

                    switch (opcode)
                    {
                        case CEE_BGE_UN: branch = (left >= right); break;
                        case CEE_BGT_UN: branch = (left > right); break;
                        case CEE_BLE_UN: branch = (left <= right); break;
                        case CEE_BLT_UN: branch = (left < right); break;
                        default: break;
                    }
                }
            }

            frame->il_offset = branch
                ? compute_inline_branch_target(frame->il_offset, delta)
                : (frame->il_offset + 5u);
            return zaclr_result_ok();
        }

        case CEE_SWITCH:
        {
            struct zaclr_stack_value value;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            uint32_t count;
            uint32_t table_offset;
            uint32_t next_offset;
            uint32_t index;
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = stack_value_to_u32(&value, &index);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            count = read_u32(frame->il_start + frame->il_offset + 1u);
            table_offset = frame->il_offset + 5u;
            next_offset = table_offset + (count * 4u);

            if (index < count)
            {
                int32_t delta = (int32_t)read_u32(frame->il_start + table_offset + (index * 4u));
                frame->il_offset = (uint32_t)((int32_t)next_offset + delta);
                return zaclr_result_ok();
            }

            frame->il_offset = next_offset;
            return zaclr_result_ok();
        }

        case CEE_ADD:
        case CEE_ADD_OVF:
        case CEE_ADD_OVF_UN:
        case CEE_SUB:
        case CEE_SUB_OVF:
        case CEE_SUB_OVF_UN:
        case CEE_MUL:
        case CEE_MUL_OVF:
        case CEE_MUL_OVF_UN:
        case CEE_DIV:
        case CEE_DIV_UN:
        case CEE_REM:
        case CEE_REM_UN:
        case CEE_AND:
        case CEE_OR:
        case CEE_XOR:
        case CEE_SHL:
        case CEE_SHR:
        case CEE_SHR_UN:
        {
            struct zaclr_stack_value right;
            struct zaclr_stack_value left;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &right);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_eval_stack_pop(&frame->eval_stack, &left);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 1u;
            if (left.kind == ZACLR_STACK_VALUE_R4 || left.kind == ZACLR_STACK_VALUE_R8
                || right.kind == ZACLR_STACK_VALUE_R4 || right.kind == ZACLR_STACK_VALUE_R8)
            {
                if (opcode == CEE_ADD || opcode == CEE_SUB || opcode == CEE_MUL || opcode == CEE_DIV || opcode == CEE_REM)
                {
                    return push_binary_r8_result(frame, opcode, &left, &right);
                }

                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            if (left.kind == ZACLR_STACK_VALUE_I8 || right.kind == ZACLR_STACK_VALUE_I8)
            {
                int64_t left_i8;
                int64_t right_i8;

                result = stack_value_to_i64(&left, &left_i8);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = stack_value_to_i64(&right, &right_i8);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                if (opcode == CEE_ADD_OVF || opcode == CEE_ADD_OVF_UN || opcode == CEE_SUB_OVF || opcode == CEE_SUB_OVF_UN || opcode == CEE_MUL_OVF || opcode == CEE_MUL_OVF_UN)
                {
                    return push_binary_i8_checked_result(context, frame, opcode, left_i8, right_i8);
                }

                return push_binary_i8_result(frame, opcode, left_i8, right_i8);
            }

            {
                int32_t left_i4;
                int32_t right_i4;

                result = stack_value_to_i32(&left, &left_i4);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = stack_value_to_i32(&right, &right_i4);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                if (opcode == CEE_ADD_OVF || opcode == CEE_ADD_OVF_UN || opcode == CEE_SUB_OVF || opcode == CEE_SUB_OVF_UN || opcode == CEE_MUL_OVF || opcode == CEE_MUL_OVF_UN)
                {
                    return push_binary_i4_checked_result(context, frame, opcode, left_i4, right_i4);
                }

                return push_binary_i4_result(frame, opcode, left_i4, right_i4);
            }
        }

        case CEE_LEAVE_S:
        {
            int8_t delta = (int8_t)frame->il_start[frame->il_offset + 1u];
            frame->il_offset = compute_short_branch_target(frame->il_offset, delta);
            return zaclr_result_ok();
        }

        case CEE_LEAVE:
        {
            int32_t delta = (int32_t)read_u32(frame->il_start + frame->il_offset + 1u);
            frame->il_offset = compute_inline_branch_target(frame->il_offset, delta);
            return zaclr_result_ok();
        }

        case CEE_NEG:
        case CEE_NOT:
        {
            struct zaclr_stack_value value;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 1u;
            if (value.kind == ZACLR_STACK_VALUE_I8)
            {
                return opcode == CEE_NEG
                    ? push_i8(frame, -value.data.i8)
                    : push_i8(frame, ~value.data.i8);
            }

            if (value.kind == ZACLR_STACK_VALUE_I4)
            {
                return opcode == CEE_NEG
                    ? push_i4(frame, -value.data.i4)
                    : push_i4(frame, ~value.data.i4);
            }

            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        case CEE_CONV_I1:
        case CEE_CONV_I2:
        case CEE_CONV_I4:
        case CEE_CONV_I:
        case CEE_CONV_I8:
        case CEE_CONV_U1:
        case CEE_CONV_U2:
        case CEE_CONV_U4:
        case CEE_CONV_U:
        case CEE_CONV_U8:
        case CEE_CONV_R4:
        case CEE_CONV_R8:
        case CEE_CONV_R_UN:
        case CEE_CONV_OVF_I1:
        case CEE_CONV_OVF_U1:
        case CEE_CONV_OVF_I2:
        case CEE_CONV_OVF_U2:
        case CEE_CONV_OVF_I4:
        case CEE_CONV_OVF_U4:
        case CEE_CONV_OVF_I8:
        case CEE_CONV_OVF_U8:
        case CEE_CONV_OVF_I1_UN:
        case CEE_CONV_OVF_I2_UN:
        case CEE_CONV_OVF_I4_UN:
        case CEE_CONV_OVF_I8_UN:
        case CEE_CONV_OVF_U1_UN:
        case CEE_CONV_OVF_U2_UN:
        case CEE_CONV_OVF_U4_UN:
        case CEE_CONV_OVF_U8_UN:
        case CEE_CONV_OVF_I:
        case CEE_CONV_OVF_U:
        case CEE_CONV_OVF_I_UN:
        case CEE_CONV_OVF_U_UN:
        {
            struct zaclr_stack_value value;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 1u;
            if (opcode == CEE_CONV_OVF_I1 || opcode == CEE_CONV_OVF_U1 || opcode == CEE_CONV_OVF_I2 || opcode == CEE_CONV_OVF_U2
                || opcode == CEE_CONV_OVF_I4 || opcode == CEE_CONV_OVF_U4 || opcode == CEE_CONV_OVF_I8 || opcode == CEE_CONV_OVF_U8
                || opcode == CEE_CONV_OVF_I1_UN || opcode == CEE_CONV_OVF_I2_UN || opcode == CEE_CONV_OVF_I4_UN || opcode == CEE_CONV_OVF_I8_UN
                || opcode == CEE_CONV_OVF_U1_UN || opcode == CEE_CONV_OVF_U2_UN || opcode == CEE_CONV_OVF_U4_UN || opcode == CEE_CONV_OVF_U8_UN
                || opcode == CEE_CONV_OVF_I || opcode == CEE_CONV_OVF_U || opcode == CEE_CONV_OVF_I_UN || opcode == CEE_CONV_OVF_U_UN)
            {
                return push_checked_converted_value(context, frame, opcode, &value);
            }

            return push_converted_value(frame, opcode, &value);
        }

        case CEE_CEQ:
        case CEE_CGT:
        case CEE_CLT:
        case CEE_CGT_UN:
        case CEE_CLT_UN:
        {
            struct zaclr_stack_value right;
            struct zaclr_stack_value left;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &right);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_eval_stack_pop(&frame->eval_stack, &left);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 2u;
            return push_compare_result(frame, opcode, &left, &right);
        }

        case CEE_RET:
        {
            struct zaclr_frame* returning_frame = frame;
            struct zaclr_frame* parent = frame->parent;

            ZACLR_TRACE_VALUE(context->runtime,
                              ZACLR_TRACE_CATEGORY_EXEC,
                              ZACLR_TRACE_EVENT_METHOD_RETURN,
                              frame->method != NULL ? frame->method->name.text : "<unknown>",
                              frame->method != NULL ? (uint64_t)frame->method->id : 0u);

            if (parent != NULL && frame->method != NULL && frame->method->signature.return_type.element_type != ZACLR_ELEMENT_TYPE_VOID)
            {
                struct zaclr_stack_value return_value;
                struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &return_value);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = zaclr_eval_stack_push(&parent->eval_stack, &return_value);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
            }
            else if (parent != NULL
                     && frame->method != NULL
                     && frame->method->name.text != NULL
                     && text_equals(frame->method->name.text, ".ctor")
                     && frame->argument_count != 0u
                     && frame->arguments != NULL
                     && frame->arguments[0].kind == ZACLR_STACK_VALUE_OBJECT_HANDLE)
            {
                struct zaclr_result result = zaclr_eval_stack_push(&parent->eval_stack, &frame->arguments[0]);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
            }

            zaclr_frame_destroy(returning_frame);
            *context->current_frame = parent;
            return zaclr_result_ok();
        }

        default:
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }
}
