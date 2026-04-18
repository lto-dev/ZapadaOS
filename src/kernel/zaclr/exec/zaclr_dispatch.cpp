#include <kernel/zaclr/exec/zaclr_dispatch.h>

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/exec/zaclr_call_resolution.h>
#include <kernel/zaclr/exec/zaclr_dispatch_helpers.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>
#include <kernel/zaclr/exec/zaclr_emulated_float.h>
#include <kernel/zaclr/exec/zaclr_interop_dispatch.h>
#include <kernel/zaclr/exec/zaclr_intrinsics.h>
#include <kernel/zaclr/exec/zaclr_type_init.h>
#include <kernel/zaclr/heap/zaclr_array.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/interop/zaclr_internal_call_registry.h>
#include <kernel/zaclr/interop/zaclr_marshalling.h>
#include <kernel/zaclr/interop/zaclr_native_assembly.h>
#include <kernel/zaclr/interop/zaclr_pinvoke_resolver.h>
#include <kernel/zaclr/interop/zaclr_qcall_table.h>
#include <kernel/zaclr/loader/zaclr_binder.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_call_target.h>
#include <kernel/zaclr/typesystem/zaclr_delegate_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_method_handle.h>
#include <kernel/zaclr/typesystem/zaclr_method_table.h>
#include <kernel/zaclr/typesystem/zaclr_type_prepare.h>
#include <kernel/zaclr/typesystem/zaclr_member_resolution.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

extern "C" {
#include <kernel/console.h>
#include <kernel/support/kernel_memory.h>
}

namespace
{
    static constexpr uint32_t k_zaclr_frame_flag_newobj_ctor = 0x00000001u;
    static constexpr uint16_t k_method_attribute_static = 0x0010u;
    static constexpr uint16_t METHOD_FLAG_VIRTUAL = 0x0040u;
    static constexpr uint8_t k_type_initializer_state_running = 1u;
    static constexpr uint8_t k_type_initializer_state_complete = 2u;

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

    static struct zaclr_result resolve_ldtoken_target(struct zaclr_frame* frame,
                                                      struct zaclr_token token,
                                                      const struct zaclr_loaded_assembly** out_assembly,
                                                      const struct zaclr_type_desc** out_type)
    {
        auto resolve_generic_argument_type = [&](const struct zaclr_generic_argument* argument) -> struct zaclr_result
        {
            if (argument == NULL || argument->assembly == NULL)
            {
                console_write("[ZACLR][ldtoken-gat] null arg or assembly\n");
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
            }

            console_write("[ZACLR][ldtoken-gat] kind=");
            console_write_dec((uint64_t)argument->kind);
            console_write(" element_type=");
            console_write_hex64((uint64_t)argument->element_type);
            console_write(" assembly=");
            console_write_hex64((uint64_t)(uintptr_t)argument->assembly);
            console_write("\n");

            /* Primitive element types (int32, bool, etc.) carry a NIL token; resolve them
               to their System.Xxx box type in System.Private.CoreLib.
               CoreCLR reference: coreclr/vm/siginfo.cpp TypeFromStack / CorTypeInfo. */
            if (argument->kind == ZACLR_GENERIC_ARGUMENT_KIND_PRIMITIVE)
            {
                const char* primitive_type_name = NULL;
                switch (argument->element_type)
                {
                    case ZACLR_ELEMENT_TYPE_BOOLEAN: primitive_type_name = "Boolean"; break;
                    case ZACLR_ELEMENT_TYPE_CHAR:    primitive_type_name = "Char";    break;
                    case ZACLR_ELEMENT_TYPE_I1:      primitive_type_name = "SByte";   break;
                    case ZACLR_ELEMENT_TYPE_U1:      primitive_type_name = "Byte";    break;
                    case ZACLR_ELEMENT_TYPE_I2:      primitive_type_name = "Int16";   break;
                    case ZACLR_ELEMENT_TYPE_U2:      primitive_type_name = "UInt16";  break;
                    case ZACLR_ELEMENT_TYPE_I4:      primitive_type_name = "Int32";   break;
                    case ZACLR_ELEMENT_TYPE_U4:      primitive_type_name = "UInt32";  break;
                    case ZACLR_ELEMENT_TYPE_I8:      primitive_type_name = "Int64";   break;
                    case ZACLR_ELEMENT_TYPE_U8:      primitive_type_name = "UInt64";  break;
                    case ZACLR_ELEMENT_TYPE_R4:      primitive_type_name = "Single";  break;
                    case ZACLR_ELEMENT_TYPE_R8:      primitive_type_name = "Double";  break;
                    case ZACLR_ELEMENT_TYPE_I:       primitive_type_name = "IntPtr";  break;
                    case ZACLR_ELEMENT_TYPE_U:       primitive_type_name = "UIntPtr"; break;
                    case ZACLR_ELEMENT_TYPE_STRING:  primitive_type_name = "String";  break;
                    case ZACLR_ELEMENT_TYPE_OBJECT:  primitive_type_name = "Object";  break;
                    default:
                        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
                }

                console_write("[ZACLR][ldtoken-prim] resolving System.");
                console_write(primitive_type_name);
                console_write("\n");

                struct zaclr_member_name_ref primitive_name_ref = {};
                primitive_name_ref.type_namespace = "System";
                primitive_name_ref.type_name      = primitive_type_name;
                {
                    struct zaclr_result prim_result =
                        zaclr_type_system_resolve_external_named_type(frame->runtime,
                                                                       "System.Private.CoreLib",
                                                                       &primitive_name_ref,
                                                                       out_assembly,
                                                                       out_type);
                    console_write("[ZACLR][ldtoken-prim-result] status=");
                    console_write_dec((uint64_t)prim_result.status);
                    console_write("\n");
                    return prim_result;
                }
            }

            if (!zaclr_token_matches_table(&argument->token, ZACLR_TOKEN_TABLE_TYPEDEF)
                && !zaclr_token_matches_table(&argument->token, ZACLR_TOKEN_TABLE_TYPEREF)
                && !zaclr_token_matches_table(&argument->token, ZACLR_TOKEN_TABLE_TYPESPEC))
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            *out_assembly = argument->assembly;
            return resolve_type_desc(argument->assembly,
                                     frame->runtime,
                                     argument->token,
                                     out_assembly,
                                     out_type);
        };

        if (frame == NULL || out_assembly == NULL || out_type == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEDEF)
            || zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPEREF))
        {
            return resolve_type_desc(frame->assembly, frame->runtime, token, out_assembly, out_type);
        }

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_TYPESPEC))
        {
            struct zaclr_result type_spec_result = resolve_type_desc(frame->assembly,
                                                                      frame->runtime,
                                                                      token,
                                                                      out_assembly,
                                                                      out_type);
            if (type_spec_result.status == ZACLR_STATUS_OK)
            {
                return type_spec_result;
            }

            /* Fallback: handle bare VAR(n) or MVAR(n) TypeSpecs by looking up
               the matching slot in the frame's generic context.  GENERICINST
               TypeSpecs are resolved by resolve_type_desc above; only bare
               element-type TypeSpecs (e.g. ELEMENT_TYPE_VAR 0 == class type
               param !0) reach here. */
            {
                const struct zaclr_metadata_table_view* spec_table =
                    &frame->assembly->metadata.tables[ZACLR_TOKEN_TABLE_TYPESPEC];
                uint32_t spec_row = zaclr_token_row(&token);

                if (spec_row > 0u
                    && spec_row <= spec_table->row_count
                    && spec_table->rows != NULL)
                {
                    const uint8_t* spec_row_data =
                        spec_table->rows + ((spec_row - 1u) * spec_table->row_size);
                    uint32_t blob_idx = (frame->assembly->metadata.heap_sizes & 0x04u) != 0u
                        ? read_u32(spec_row_data) : read_u16(spec_row_data);
                    struct zaclr_slice spec_blob = {};

                    if (zaclr_metadata_reader_get_blob(&frame->assembly->metadata,
                                                       blob_idx,
                                                       &spec_blob).status == ZACLR_STATUS_OK
                        && spec_blob.size >= 2u)
                    {
                        uint8_t elem_type = spec_blob.data[0];
                        uint32_t generic_idx = 0u;

                        /* Decode the compressed-uint parameter index. */
                        if (spec_blob.data[1] < 0x80u)
                        {
                            generic_idx = (uint32_t)spec_blob.data[1];
                        }
                        else if (spec_blob.size >= 3u
                                 && (spec_blob.data[1] & 0xC0u) == 0x80u)
                        {
                            generic_idx = (((uint32_t)(spec_blob.data[1] & 0x3Fu)) << 8u)
                                        | (uint32_t)spec_blob.data[2];
                        }

                        console_write("[ZACLR][ldtoken-blob] elem_type=");
                        console_write_hex64((uint64_t)elem_type);
                        console_write(" generic_idx=");
                        console_write_dec((uint64_t)generic_idx);
                        console_write(" type_arg_count=");
                        console_write_dec((uint64_t)frame->generic_context.type_arg_count);
                        console_write(" type_args_ptr=");
                        console_write_hex64((uint64_t)(uintptr_t)frame->generic_context.type_args);
                        console_write("\n");

                        if (elem_type == ZACLR_ELEMENT_TYPE_VAR
                            && frame->generic_context.type_args != NULL
                            && generic_idx < frame->generic_context.type_arg_count)
                        {
                            return resolve_generic_argument_type(
                                &frame->generic_context.type_args[generic_idx]);
                        }

                        if (elem_type == ZACLR_ELEMENT_TYPE_MVAR
                            && frame->generic_context.method_args != NULL
                            && generic_idx < frame->generic_context.method_arg_count)
                        {
                            return resolve_generic_argument_type(
                                &frame->generic_context.method_args[generic_idx]);
                        }
                    }
                }
            }

            return type_spec_result;
        }

        if (frame->runtime != NULL
            && (token.raw & 0xFF000000u) == 0u
            && (frame->generic_context.type_arg_count > 0u || frame->generic_context.method_arg_count > 0u))
        {
            uint32_t generic_index = token.raw & 0x00FFFFFFu;

            if (generic_index < frame->generic_context.type_arg_count)
            {
                const struct zaclr_generic_argument* type_arg = &frame->generic_context.type_args[generic_index];
                struct zaclr_result type_result = resolve_generic_argument_type(type_arg);
                if (type_result.status == ZACLR_STATUS_OK)
                {
                    return type_result;
                }
            }

            if (generic_index < frame->generic_context.method_arg_count)
            {
                const struct zaclr_generic_argument* method_arg = &frame->generic_context.method_args[generic_index];
                struct zaclr_result method_result = resolve_generic_argument_type(method_arg);
                if (method_result.status == ZACLR_STATUS_OK)
                {
                    return method_result;
                }
            }
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    static const struct zaclr_type_desc* find_field_owner_type(const struct zaclr_loaded_assembly* assembly,
                                                               uint32_t field_row)
    {
        uint32_t type_index;

        if (assembly == NULL || assembly->type_map.types == NULL || field_row == 0u)
        {
            return NULL;
        }

        for (type_index = 0u; type_index < assembly->type_map.count; ++type_index)
        {
            const struct zaclr_type_desc* type = &assembly->type_map.types[type_index];
            if (field_row >= type->field_list && field_row < (type->field_list + type->field_count))
            {
                return type;
            }
        }

        return NULL;
    }

    static struct zaclr_result ensure_type_initializer_ran(struct zaclr_runtime* runtime,
                                                           const struct zaclr_loaded_assembly* assembly,
                                                           const struct zaclr_type_desc* type)
    {
        return zaclr_ensure_type_initializer_ran(runtime, assembly, type);
    }

    static bool method_is_type_initializer_trigger(const struct zaclr_method_desc* method)
    {
        return zaclr_method_is_type_initializer_trigger(method);
    }

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
            console_write("[ZACLR][ldstr] invalid_arg reader=");
            console_write_hex64((uint64_t)(uintptr_t)reader);
            console_write(" data=");
            console_write_hex64(reader != NULL ? (uint64_t)(uintptr_t)reader->user_string_heap.data : 0u);
            console_write(" size=");
            console_write_dec(reader != NULL ? (uint64_t)reader->user_string_heap.size : 0u);
            console_write(" index=");
            console_write_dec((uint64_t)index);
            console_write("\n");
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

        result = zaclr_string_allocate_utf16_handle(heap, text, char_length, out_handle);
        kernel_free(text);
        return result;
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

            result = ensure_type_initializer_ran(frame->runtime,
                                                 target_assembly,
                                                 find_field_owner_type(target_assembly, target_field_row));
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
        {
            struct zaclr_result init_result = ensure_type_initializer_ran(frame->runtime,
                                                                          frame->assembly,
                                                                          find_field_owner_type(frame->assembly, field_row));
            if (init_result.status != ZACLR_STATUS_OK)
            {
                return init_result;
            }
        }
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
        struct zaclr_stack_value value = {};

        if (frame == NULL || local_index >= frame->local_count)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        {
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }
        }

        frame->locals[local_index] = value;

        return zaclr_result_ok();
    }

    static struct zaclr_result store_argument(struct zaclr_frame* frame, uint32_t argument_index)
    {
        struct zaclr_stack_value value = {};

        if (frame == NULL || argument_index >= frame->argument_count)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        {
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }
        }

        if (frame->arguments[argument_index].kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
        {
            struct zaclr_stack_value* target = (struct zaclr_stack_value*)(uintptr_t)frame->arguments[argument_index].data.raw;
            if (target == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            ZACLR_TRACE_VALUE(frame->runtime,
                              ZACLR_TRACE_CATEGORY_EXEC,
                              ZACLR_TRACE_EVENT_CALL_TARGET,
                              "StoreArgument.ByRefWriteKind",
                              (uint64_t)value.kind);
            *target = value;
            return zaclr_result_ok();
        }

        frame->arguments[argument_index] = value;
        return zaclr_result_ok();
    }

    static struct zaclr_result store_static_field(struct zaclr_frame* frame, struct zaclr_token token)
    {
        uint32_t field_row;
        struct zaclr_result result;
        struct zaclr_memberref_target memberref = {};
        const struct zaclr_loaded_assembly* target_assembly = NULL;
        uint32_t target_field_row = 0u;

        if (frame == NULL || frame->assembly == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_MEMBERREF))
        {
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

            result = ensure_type_initializer_ran(frame->runtime,
                                                 target_assembly,
                                                 find_field_owner_type(target_assembly, target_field_row));
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (target_field_row == 0u || target_field_row > target_assembly->static_field_count || target_assembly->static_fields == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
            }

            return zaclr_eval_stack_pop(&frame->eval_stack, &target_assembly->static_fields[target_field_row - 1u]);
        }

        if (!zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        field_row = zaclr_token_row(&token);
        result = ensure_type_initializer_ran(frame->runtime,
                                             frame->assembly,
                                             find_field_owner_type(frame->assembly, field_row));
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (field_row == 0u || field_row > frame->assembly->static_field_count || frame->assembly->static_fields == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
        }

        return zaclr_eval_stack_pop(&frame->eval_stack, &frame->assembly->static_fields[field_row - 1u]);
    }

    static struct zaclr_result store_object_field(struct zaclr_runtime* runtime,
                                                  struct zaclr_object_desc* object,
                                                  struct zaclr_token token,
                                                  const struct zaclr_stack_value* value)
    {
        return zaclr_object_store_field(runtime, object, token, value);
    }

    static struct zaclr_result push_object_handle(struct zaclr_frame* frame, zaclr_object_handle value)
    {
        struct zaclr_stack_value stack_value = {};
        stack_value.kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        stack_value.data.object_reference = frame != NULL && frame->runtime != NULL
            ? zaclr_heap_get_object(&frame->runtime->heap, value)
            : NULL;
        return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
    }

    static struct zaclr_result push_local_address(struct zaclr_frame* frame, uint32_t local_index)
    {
        struct zaclr_stack_value stack_value = {};
        struct zaclr_result result;

        if (frame == NULL || local_index >= frame->local_count)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = zaclr_stack_value_set_byref(&stack_value,
                                             &frame->locals[local_index],
                                             sizeof(struct zaclr_stack_value),
                                             0u,
                                             ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
    }

    static struct zaclr_result push_argument_address(struct zaclr_frame* frame, uint32_t argument_index)
    {
        struct zaclr_stack_value stack_value = {};
        struct zaclr_result result;

        if (frame == NULL || argument_index >= frame->argument_count)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = zaclr_stack_value_set_byref(&stack_value,
                                             &frame->arguments[argument_index],
                                             sizeof(struct zaclr_stack_value),
                                             0u,
                                             ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
    }

    static struct zaclr_stack_value* resolve_local_address_target(struct zaclr_frame* frame,
                                                                  struct zaclr_stack_value* value)
    {
        uint32_t local_index;

        if (frame == NULL || value == NULL)
        {
            return NULL;
        }

        if (value->kind == ZACLR_STACK_VALUE_BYREF)
        {
            return (value->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) != 0u
                ? (struct zaclr_stack_value*)(uintptr_t)value->data.raw
                : NULL;
        }

        if (value->kind != ZACLR_STACK_VALUE_LOCAL_ADDRESS)
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

    struct zaclr_resolved_byref {
        struct zaclr_stack_value* stack_slot;
        uint8_t* address;
        uint32_t payload_size;
        uint32_t type_token_raw;
    };

    static bool stack_value_kind_is_scalar(uint32_t kind)
    {
        return kind == ZACLR_STACK_VALUE_I4
            || kind == ZACLR_STACK_VALUE_I8
            || kind == ZACLR_STACK_VALUE_R4
            || kind == ZACLR_STACK_VALUE_R8
            || kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE;
    }

    static struct zaclr_result resolve_byref_target(struct zaclr_frame* frame,
                                                    struct zaclr_stack_value* value,
                                                    struct zaclr_resolved_byref* out_target)
    {
        if (value == NULL || out_target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_target = {};
        if (value->kind == ZACLR_STACK_VALUE_BYREF)
        {
            out_target->payload_size = value->payload_size;
            out_target->type_token_raw = value->type_token_raw;
            if ((value->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) != 0u)
            {
                out_target->stack_slot = (struct zaclr_stack_value*)(uintptr_t)value->data.raw;
                return out_target->stack_slot != NULL
                    ? zaclr_result_ok()
                    : zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            out_target->address = (uint8_t*)(uintptr_t)value->data.raw;
            return out_target->address != NULL
                ? zaclr_result_ok()
                : zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (value->kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
        {
            out_target->stack_slot = resolve_local_address_target(frame, value);
            out_target->payload_size = sizeof(struct zaclr_stack_value);
            return out_target->stack_slot != NULL
                ? zaclr_result_ok()
                : zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (value->kind == ZACLR_STACK_VALUE_VALUETYPE)
        {
            out_target->address = (uint8_t*)zaclr_stack_value_payload(value);
            out_target->payload_size = value->payload_size;
            out_target->type_token_raw = value->type_token_raw;
            return out_target->address != NULL
                ? zaclr_result_ok()
                : zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        /* Native int / pointer used as a managed pointer (from Unsafe.AsPointer etc.) */
        if (value->kind == ZACLR_STACK_VALUE_I8 && value->data.i8 != 0)
        {
            out_target->address = (uint8_t*)(uintptr_t)value->data.i8;
            out_target->payload_size = value->payload_size != 0u ? value->payload_size : 8u;
            out_target->type_token_raw = value->type_token_raw;
            return zaclr_result_ok();
        }

        if (value->kind == ZACLR_STACK_VALUE_I4 && value->data.i4 != 0)
        {
            out_target->address = (uint8_t*)(uintptr_t)(uint32_t)value->data.i4;
            out_target->payload_size = value->payload_size != 0u ? value->payload_size : 4u;
            out_target->type_token_raw = value->type_token_raw;
            return zaclr_result_ok();
        }

        console_write("[ZACLR][byref] resolve miss kind=");
        console_write_dec((uint64_t)value->kind);
        console_write(" flags=");
        console_write_hex64((uint64_t)value->flags);
        console_write(" payload=");
        console_write_dec((uint64_t)value->payload_size);
        console_write(" raw=");
        console_write_hex64((uint64_t)value->data.raw);
        console_write(" type_token=");
        console_write_hex64((uint64_t)value->type_token_raw);
        console_write("\n");
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
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

        return zaclr_boxed_value_allocate_handle(&runtime->heap, token, value, out_handle);
    }

    static const struct zaclr_field_layout* find_instance_field_layout_in_method_table(const struct zaclr_method_table* method_table,
                                                                                       struct zaclr_token token)
    {
        uint32_t index;

        if (method_table == NULL || method_table->instance_fields == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
        {
            return NULL;
        }

        for (index = 0u; index < method_table->instance_field_count; ++index)
        {
            const struct zaclr_field_layout* layout = &method_table->instance_fields[index];
            if (layout->is_static == 0u && layout->field_token_row == zaclr_token_row(&token))
            {
                return layout;
            }
        }

        return NULL;
    }

    static struct zaclr_result load_field_from_resolved_byref_payload(struct zaclr_frame* frame,
                                                                      const struct zaclr_resolved_byref* target,
                                                                      struct zaclr_token field_token,
                                                                      struct zaclr_stack_value* out_value)
    {
        const struct zaclr_loaded_assembly* type_assembly = NULL;
        const struct zaclr_type_desc* type_desc = NULL;
        struct zaclr_method_table* method_table = NULL;
        const struct zaclr_field_layout* layout;
        const uint8_t* field_address;

        if (frame == NULL || target == NULL || out_value == NULL || target->address == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        {
            struct zaclr_token value_type_token = zaclr_token_make(target->type_token_raw);
            if (!zaclr_token_matches_table(&field_token, ZACLR_TOKEN_TABLE_FIELD))
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            if (zaclr_token_matches_table(&value_type_token, ZACLR_TOKEN_TABLE_TYPEDEF)
                || zaclr_token_matches_table(&value_type_token, ZACLR_TOKEN_TABLE_TYPEREF)
                || zaclr_token_matches_table(&value_type_token, ZACLR_TOKEN_TABLE_TYPESPEC))
            {
                struct zaclr_result result = zaclr_type_system_resolve_type_desc(frame->assembly,
                                                                                 frame->runtime,
                                                                                 value_type_token,
                                                                                 &type_assembly,
                                                                                 &type_desc);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
            }
            else
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }
        }
        if (type_desc == NULL || type_assembly == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
        }

        {
            struct zaclr_result result = zaclr_type_prepare(frame->runtime,
                                                           (struct zaclr_loaded_assembly*)type_assembly,
                                                           type_desc,
                                                           &method_table);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }
        }

        layout = find_instance_field_layout_in_method_table(method_table, field_token);
        if (layout == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
        }

        field_address = target->address + layout->byte_offset;
        *out_value = {};

        if (layout->element_type == ZACLR_ELEMENT_TYPE_VALUETYPE)
        {
            return zaclr_stack_value_set_valuetype(out_value,
                                                   layout->nested_type_token_raw,
                                                   field_address,
                                                   layout->field_size);
        }

        if (layout->is_reference != 0u)
        {
            struct zaclr_object_desc* reference = NULL;
            kernel_memcpy(&reference, field_address, sizeof(reference));
            out_value->kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
            out_value->data.object_reference = reference;
            return zaclr_result_ok();
        }

        if (layout->field_size <= 4u)
        {
            uint32_t raw = 0u;
            kernel_memcpy(&raw, field_address, layout->field_size);
            out_value->kind = ZACLR_STACK_VALUE_I4;
            out_value->data.i4 = (int32_t)raw;
            return zaclr_result_ok();
        }

        if (layout->field_size == 8u)
        {
            uint64_t raw = 0u;
            kernel_memcpy(&raw, field_address, sizeof(raw));
            out_value->kind = ZACLR_STACK_VALUE_I8;
            out_value->data.i8 = (int64_t)raw;
            return zaclr_result_ok();
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    static struct zaclr_result allocate_reference_type_instance(struct zaclr_runtime* runtime,
                                                                const struct zaclr_loaded_assembly* owning_assembly,
                                                                struct zaclr_token type_token,
                                                                zaclr_object_handle* out_handle)
    {
        struct zaclr_object_desc* object = NULL;
        struct zaclr_result result;

        if (out_handle == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_handle = 0u;
        result = zaclr_allocate_reference_type_instance(runtime, owning_assembly, type_token, &object);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_handle = runtime != NULL ? zaclr_heap_get_object_handle(&runtime->heap, object) : 0u;
        return zaclr_result_ok();
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
        return zaclr_dispatch_resolve_type_desc(current_assembly, runtime, token, out_assembly, out_type);
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

    static bool reference_type_matches_target(struct zaclr_runtime* runtime,
                                              const struct zaclr_loaded_assembly* source_assembly,
                                              const struct zaclr_loaded_assembly* target_assembly,
                                              const struct zaclr_type_desc* source_type,
                                              const struct zaclr_type_desc* target_type)
    {
        const struct zaclr_loaded_assembly* current_assembly = source_assembly;
        const struct zaclr_type_desc* current_type = source_type;

        while (current_type != NULL && current_assembly != NULL)
        {
            const struct zaclr_loaded_assembly* base_assembly = NULL;
            const struct zaclr_type_desc* base_type = NULL;
            struct zaclr_result base_result;

            if (current_assembly == target_assembly && current_type->token.raw == target_type->token.raw)
            {
                return true;
            }

            if (zaclr_token_is_nil(&current_type->extends))
            {
                break;
            }

            base_result = resolve_type_desc(current_assembly,
                                            runtime,
                                            current_type->extends,
                                            &base_assembly,
                                            &base_type);
            if (base_result.status != ZACLR_STATUS_OK || base_type == NULL)
            {
                break;
            }

            current_assembly = base_assembly;
            current_type = base_type;
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
            const struct zaclr_loaded_assembly* source_assembly;
            const struct zaclr_loaded_assembly* target_assembly;
            const struct zaclr_type_desc* source_type;
            const struct zaclr_type_desc* target_type;
            struct zaclr_token source_token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | object->type_id);
            struct zaclr_result result = resolve_type_desc(current_assembly, runtime, target_token, &target_assembly, &target_type);
            if (result.status != ZACLR_STATUS_OK)
            {
                return false;
            }

            source_assembly = object->owning_assembly != NULL ? object->owning_assembly : current_assembly;
            source_type = zaclr_type_map_find_by_token(&source_assembly->type_map, source_token);
            if (source_type == NULL)
            {
                return false;
            }

            return reference_type_matches_target(runtime,
                                                 source_assembly,
                                                 target_assembly,
                                                 source_type,
                                                 target_type);
        }

        return false;
    }

    static struct zaclr_result load_object_field(struct zaclr_runtime* runtime,
                                                 const struct zaclr_object_desc* object,
                                                 struct zaclr_token token,
                                                 struct zaclr_stack_value* out_value)
    {
        return zaclr_object_load_field(runtime, object, token, out_value);
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

            child->arguments[0].kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
            child->arguments[0].data.object_reference = child->runtime != NULL
                ? zaclr_heap_get_object(&child->runtime->heap, instance_handle)
                : NULL;
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
        return zaclr_dispatch_is_system_object_ctor(memberref);
    }

    static struct zaclr_result try_invoke_intrinsic(struct zaclr_frame* frame,
                                                    const struct zaclr_loaded_assembly* assembly,
                                                    const struct zaclr_type_desc* type,
                                                    const struct zaclr_method_desc* method)
    {
        return zaclr_try_invoke_intrinsic(frame, assembly, type, method);
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

        if (value->kind == ZACLR_STACK_VALUE_VALUETYPE)
        {
            const void* payload = zaclr_stack_value_payload_const(value);
            if (payload != NULL)
            {
                if (value->payload_size <= 4u)
                {
                    uint32_t raw = 0u;
                    kernel_memcpy(&raw, payload, value->payload_size);
                    *out_value = (int32_t)raw;
                    return zaclr_result_ok();
                }

                if (value->payload_size == 8u)
                {
                    uint64_t raw = 0u;
                    kernel_memcpy(&raw, payload, sizeof(raw));
                    *out_value = (int32_t)raw;
                    return zaclr_result_ok();
                }
            }
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
        uintptr_t raw_handle = 0u;
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

            if (target->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            *out_handle = zaclr_heap_get_object_handle(&runtime->heap, target->data.object_reference);
            return zaclr_result_ok();
        }

        if (address_value->kind == ZACLR_STACK_VALUE_BYREF)
        {
            if ((address_value->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) != 0u)
            {
                target = (struct zaclr_stack_value*)(uintptr_t)address_value->data.raw;
                if (target == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }

                console_write("[ZACLR][ldind.ref.slot] target_kind=");
                console_write_dec((uint64_t)target->kind);
                console_write(" target_flags=");
                console_write_hex64((uint64_t)target->flags);
                console_write(" target_payload=");
                console_write_dec((uint64_t)target->payload_size);
                console_write(" target_raw=");
                console_write_hex64((uint64_t)target->data.raw);
                console_write(" target_type_token=");
                console_write_hex64((uint64_t)target->type_token_raw);
                console_write("\n");

                if (target->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
                {
                    *out_handle = zaclr_heap_get_object_handle(&runtime->heap, target->data.object_reference);
                    return zaclr_result_ok();
                }

                if (target->kind == ZACLR_STACK_VALUE_VALUETYPE)
                {
                    const void* payload = zaclr_stack_value_payload_const(target);
                    if (payload != NULL && target->payload_size >= sizeof(void*))
                    {
                        struct zaclr_object_desc* direct_reference = NULL;
                        kernel_memcpy(&direct_reference, payload, sizeof(direct_reference));
                        *out_handle = zaclr_heap_get_object_handle(&runtime->heap, direct_reference);
                        return direct_reference != NULL
                            ? zaclr_result_ok()
                            : zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                    }
                }

                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            raw_handle = (uintptr_t)address_value->data.raw;
            if (raw_handle == 0u)
            {
                *out_handle = 0u;
                return zaclr_result_ok();
            }

            {
                struct zaclr_object_desc* direct_reference = *(struct zaclr_object_desc**)(uintptr_t)raw_handle;
                zaclr_object_handle direct_handle = zaclr_heap_get_object_handle(&runtime->heap, direct_reference);
                if (direct_reference == NULL)
                {
                    *out_handle = 0u;
                    return zaclr_result_ok();
                }
                if (direct_handle != 0u)
                {
                    *out_handle = direct_handle;
                    return zaclr_result_ok();
                }
            }

            encoded_handle = (uint64_t)raw_handle;
        }
        else if (address_value->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        {
            *out_handle = zaclr_heap_get_object_handle(&runtime->heap, address_value->data.object_reference);
            return zaclr_result_ok();
        }
        else if (address_value->kind == ZACLR_STACK_VALUE_I8)
        {
            encoded_handle = (uint64_t)address_value->data.i8;
            raw_handle = (uintptr_t)address_value->data.i8;
        }
        else if (address_value->kind == ZACLR_STACK_VALUE_I4)
        {
            encoded_handle = (uint32_t)address_value->data.i4;
            raw_handle = (uintptr_t)(uint32_t)address_value->data.i4;
        }
        else
        {
            console_write("[ZACLR][ldind.ref] unsupported address kind=");
            console_write_dec((uint64_t)address_value->kind);
            console_write(" flags=");
            console_write_hex64((uint64_t)address_value->flags);
            console_write(" raw=");
            console_write_hex64((uint64_t)address_value->data.raw);
            console_write(" type_token=");
            console_write_hex64((uint64_t)address_value->type_token_raw);
            console_write("\n");
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (encoded_handle == 0u)
        {
            *out_handle = 0u;
            return zaclr_result_ok();
        }

        /* If this is a raw native pointer from Unsafe.AsPointer / QCall wrapper flow,
           try treating it as an address that directly contains an object reference. */
        if (raw_handle != 0u)
        {
            struct zaclr_object_desc* direct_reference = *(struct zaclr_object_desc**)(uintptr_t)raw_handle;
            zaclr_object_handle direct_handle = zaclr_heap_get_object_handle(&runtime->heap, direct_reference);
            if (direct_reference == NULL)
            {
                *out_handle = 0u;
                return zaclr_result_ok();
            }
            if (direct_handle != 0u)
            {
                *out_handle = direct_handle;
                return zaclr_result_ok();
            }
        }

        if (runtime->boot_launch.handle_table.entries != NULL)
        {
            uintptr_t base = (uintptr_t)&runtime->boot_launch.handle_table.entries[0].handle;
            uintptr_t end = (uintptr_t)&runtime->boot_launch.handle_table.entries[runtime->boot_launch.handle_table.capacity].handle;

            if (raw_handle >= base
                && raw_handle < end
                && ((raw_handle - base) % sizeof(struct zaclr_gc_handle_entry)) == 0u)
            {
                handle_index = (uint32_t)((raw_handle - base) / sizeof(struct zaclr_gc_handle_entry));
            }
            else if (!decode_encoded_gc_handle_value(encoded_handle, &handle_index))
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }
        }
        else if (!decode_encoded_gc_handle_value(encoded_handle, &handle_index))
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

        if (value->kind == ZACLR_STACK_VALUE_VALUETYPE)
        {
            const void* payload = zaclr_stack_value_payload_const(value);
            if (payload != NULL)
            {
                if (value->payload_size <= 4u)
                {
                    uint32_t raw = 0u;
                    kernel_memcpy(&raw, payload, value->payload_size);
                    *out_value = (int64_t)(uint64_t)raw;
                    return zaclr_result_ok();
                }

                if (value->payload_size == 8u)
                {
                    uint64_t raw = 0u;
                    kernel_memcpy(&raw, payload, sizeof(raw));
                    *out_value = (int64_t)raw;
                    return zaclr_result_ok();
                }
            }
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

        if (left.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE && right.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        {
            zaclr_object_handle left_handle = zaclr_heap_get_object_handle(&frame->runtime->heap, left.data.object_reference);
            zaclr_object_handle right_handle = zaclr_heap_get_object_handle(&frame->runtime->heap, right.data.object_reference);
            ZACLR_TRACE_VALUE(frame->runtime,
                              ZACLR_TRACE_CATEGORY_EXEC,
                              ZACLR_TRACE_EVENT_CALL_TARGET,
                              "BranchCompare.Object.Left",
                              (uint64_t)left_handle);
            ZACLR_TRACE_VALUE(frame->runtime,
                              ZACLR_TRACE_CATEGORY_EXEC,
                              ZACLR_TRACE_EVENT_CALL_TARGET,
                              "BranchCompare.Object.Right",
                              (uint64_t)right_handle);
            *out_left = (int32_t)left_handle;
            *out_right = (int32_t)right_handle;
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

    static struct zaclr_result stack_value_to_object_reference(struct zaclr_runtime*,
                                                               const struct zaclr_stack_value* value,
                                                               struct zaclr_object_desc** out_value)
    {
        if (value == NULL || out_value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_value = value->data.object_reference;
        return zaclr_result_ok();
    }

    static struct zaclr_result stack_value_to_object_handle(struct zaclr_runtime* runtime,
                                                            const struct zaclr_stack_value* value,
                                                            zaclr_object_handle* out_value)
    {
        struct zaclr_object_desc* object = NULL;
        struct zaclr_result result;

        if (runtime == NULL || out_value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = stack_value_to_object_reference(runtime, value, &object);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_value = zaclr_heap_get_object_handle(&runtime->heap, object);
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

            *out_handle = zaclr_heap_get_object_handle(&runtime->heap, exception_object);
            runtime->boot_launch.thread.current_exception = exception_object;
        }

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

        if (left->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE || right->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        {
            zaclr_object_handle left_handle = left->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE
                ? zaclr_heap_get_object_handle(&frame->runtime->heap, left->data.object_reference)
                : 0u;
            zaclr_object_handle right_handle = right->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE
                ? zaclr_heap_get_object_handle(&frame->runtime->heap, right->data.object_reference)
                : 0u;

            ZACLR_TRACE_VALUE(frame->runtime,
                              ZACLR_TRACE_CATEGORY_EXEC,
                              ZACLR_TRACE_EVENT_CALL_TARGET,
                              "Compare.Object.Left",
                              (uint64_t)left_handle);
            ZACLR_TRACE_VALUE(frame->runtime,
                              ZACLR_TRACE_CATEGORY_EXEC,
                              ZACLR_TRACE_EVENT_CALL_TARGET,
                              "Compare.Object.Right",
                              (uint64_t)right_handle);

            switch (opcode)
            {
                case CEE_CEQ:
                    result_value = (left_handle == right_handle) ? 1 : 0;
                    break;
                case CEE_CGT_UN:
                    result_value = (left_handle != right_handle && left_handle != 0u) ? 1 : 0;
                    break;
                case CEE_CLT_UN:
                    result_value = (right_handle != left_handle && right_handle != 0u) ? 1 : 0;
                    break;
                default:
                    return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            return push_i4(frame, result_value);
        }

        if (left->kind == ZACLR_STACK_VALUE_I8 || right->kind == ZACLR_STACK_VALUE_I8
            || left->kind == ZACLR_STACK_VALUE_VALUETYPE || right->kind == ZACLR_STACK_VALUE_VALUETYPE)
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

    static struct zaclr_result write_array_indexed_value(struct zaclr_runtime* runtime,
                                                         struct zaclr_array_desc* array,
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

        if (runtime == NULL || array == NULL || value == NULL)
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
                result = stack_value_to_object_handle(runtime, value, &value_object);
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
                        result = stack_value_to_object_handle(runtime, value, &value_object);
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
        return zaclr_invoke_internal_call_exact(context, frame, target_assembly, target_type, target_method, invocation_kind);
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

    if (frame->method != NULL
        && frame->method->token.raw == 0x06000A4Bu
        && frame->il_offset <= 12u)
    {
        console_write("[ZACLR][trace] Type.cctor il_offset=");
        console_write_dec((uint64_t)frame->il_offset);
        console_write(" opcode=");
        console_write_hex64((uint64_t)opcode);
        console_write(" depth=");
        console_write_dec((uint64_t)frame->eval_stack.depth);
        console_write(" locals=");
        console_write_dec((uint64_t)frame->local_count);
        console_write(" args=");
        console_write_dec((uint64_t)frame->argument_count);
        console_write("\n");
    }

    switch (opcode)
    {
        case CEE_NOP:
            frame->il_offset += 1u;
            return zaclr_result_ok();

        case CEE_READONLY:
        case CEE_VOLATILE:
        case CEE_TAILCALL:
            frame->il_offset += 2u;
            return zaclr_result_ok();

        case CEE_UNALIGNED:
            frame->il_offset += 3u;
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

        case CEE_LDARGA_S:
        {
            uint32_t argument_index = frame->il_start[frame->il_offset + 1u];
            frame->il_offset += 2u;
            return push_argument_address(frame, argument_index);
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

            console_write("[ZACLR][ldind.ref] addr_kind=");
            console_write_dec((uint64_t)address_value.kind);
            console_write(" flags=");
            console_write_hex64((uint64_t)address_value.flags);
            console_write(" payload=");
            console_write_dec((uint64_t)address_value.payload_size);
            console_write(" raw=");
            console_write_hex64((uint64_t)address_value.data.raw);
            console_write(" type_token=");
            console_write_hex64((uint64_t)address_value.type_token_raw);
            console_write("\n");
            frame->il_offset += 1u;
            result = load_indirect_ref(frame->runtime, frame, &address_value, &handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            return push_object_handle(frame, handle);
        }

        case CEE_LDIND_U1:
        {
            struct zaclr_stack_value address_value = {};
            struct zaclr_stack_value* target = NULL;
            int32_t value = 0;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &address_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (address_value.kind != ZACLR_STACK_VALUE_LOCAL_ADDRESS && address_value.kind != ZACLR_STACK_VALUE_BYREF)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            frame->il_offset += 1u;
            target = resolve_local_address_target(frame, &address_value);
            if (target == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            switch (target->kind)
            {
                case ZACLR_STACK_VALUE_EMPTY:
                    value = 0;
                    break;
                case ZACLR_STACK_VALUE_I4:
                    value = (int32_t)((uint32_t)target->data.i4 & 0xFFu);
                    break;
                case ZACLR_STACK_VALUE_I8:
                    value = (int32_t)((uint64_t)target->data.i8 & 0xFFu);
                    break;
                default:
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            return push_i4(frame, value);
        }

        case CEE_LDIND_U2:
        {
            struct zaclr_stack_value address_value = {};
            struct zaclr_stack_value* target = NULL;
            int32_t value = 0;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &address_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (address_value.kind != ZACLR_STACK_VALUE_LOCAL_ADDRESS && address_value.kind != ZACLR_STACK_VALUE_BYREF)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            frame->il_offset += 1u;
            if (address_value.kind == ZACLR_STACK_VALUE_BYREF
                && (address_value.flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) == 0u)
            {
                const uint16_t* raw_value = (const uint16_t*)(uintptr_t)address_value.data.raw;
                if (raw_value == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }

                return push_i4(frame, (int32_t)(uint32_t)(*raw_value));
            }

            target = resolve_local_address_target(frame, &address_value);
            if (target == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            switch (target->kind)
            {
                case ZACLR_STACK_VALUE_EMPTY:
                    value = 0;
                    break;
                case ZACLR_STACK_VALUE_I4:
                    value = (int32_t)((uint32_t)target->data.i4 & 0xFFFFu);
                    break;
                case ZACLR_STACK_VALUE_I8:
                    value = (int32_t)((uint64_t)target->data.i8 & 0xFFFFu);
                    break;
                default:
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            return push_i4(frame, value);
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
                console_write("[ZACLR][ldstr] fail assembly_ptr=");
                console_write_hex64((uint64_t)(uintptr_t)frame->assembly);
                console_write(" assembly_id=");
                console_write_dec(frame->assembly != NULL ? (uint64_t)frame->assembly->id : 0u);
                console_write(" token=");
                console_write_hex64((uint64_t)token);
                console_write("\n");
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
            struct zaclr_slice methodspec_instantiation_blob = {};
            uint8_t has_methodspec_instantiation = 0u;
            struct zaclr_result intrinsic_result;
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

                {
                    struct zaclr_generic_context methodspec_context = {};
                    result = zaclr_generic_context_set_method_instantiation(&methodspec_context,
                                                                            frame->assembly,
                                                                            context->runtime,
                                                                            &instantiation_blob);
                    zaclr_generic_context_reset(&methodspec_context);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result.status == ZACLR_STATUS_NOT_IMPLEMENTED
                            ? zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC)
                            : result;
                    }
                }

                method_tag = methodspec_row.method_coded_index & 0x1u;
                method_row = methodspec_row.method_coded_index >> 1u;
                if (token.raw == 0x2B00017Au)
                {
                    console_write("[ZACLR][call] MethodSpec 0x2B00017A coded_index=");
                    console_write_hex64((uint64_t)methodspec_row.method_coded_index);
                    console_write(" method_rows=");
                    console_write_dec((uint64_t)zaclr_metadata_reader_get_row_count(&frame->assembly->metadata, ZACLR_TOKEN_TABLE_METHOD));
                    console_write(" memberref_rows=");
                    console_write_dec((uint64_t)zaclr_metadata_reader_get_row_count(&frame->assembly->metadata, ZACLR_TOKEN_TABLE_MEMBERREF));
                    console_write(" param_rows=");
                    console_write_dec((uint64_t)zaclr_metadata_reader_get_row_count(&frame->assembly->metadata, 0x08u));
                    console_write(" inst_blob_size=");
                    console_write_dec((uint64_t)instantiation_blob.size);
                    console_write(" inst_arg_count=");
                    console_write_dec((uint64_t)instantiation.argument_count);
                    console_write(" first_byte=");
                    console_write_hex64((uint64_t)(instantiation_blob.size > 0u ? instantiation_blob.data[0] : 0u));
                    console_write("\n");
                }
                if (method_row == 0u)
                {
                    return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
                }

                resolved_token = zaclr_token_make(((uint32_t)(method_tag == 0u ? ZACLR_TOKEN_TABLE_METHOD : ZACLR_TOKEN_TABLE_MEMBERREF) << 24) | method_row);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "MethodSpec.MethodTag",
                                  (uint64_t)method_tag);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "MethodSpec.ResolvedToken",
                                  (uint64_t)resolved_token.raw);
                methodspec_instantiation_blob = instantiation_blob;
                has_methodspec_instantiation = 1u;
            }

            if (zaclr_token_matches_table(&resolved_token, ZACLR_TOKEN_TABLE_METHOD))
            {
                const struct zaclr_method_desc* method;
                struct zaclr_loaded_assembly* target_assembly = NULL;
                struct zaclr_frame* child;
                struct zaclr_result result;

                method = zaclr_method_map_find_by_token(&frame->assembly->method_map, resolved_token);
                if (method == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
                }

                if (token.raw == 0x2B00017Au)
                {
                    struct zaclr_methoddef_row debug_row = {};
                    struct zaclr_signature_type parameter_type = {};
                    uint32_t resolved_row = zaclr_token_row(&resolved_token);
                    console_write("[ZACLR][call] MethodSpec 0x2B00017A resolved_name=");
                    console_write(method->name.text != NULL ? method->name.text : "<null>");
                    console_write(" param_count=");
                    console_write_dec((uint64_t)method->signature.parameter_count);
                    console_write(" callconv=");
                    console_write_hex64((uint64_t)method->signature.calling_convention);
                    if (zaclr_signature_read_method_parameter(&method->signature, 0u, &parameter_type).status == ZACLR_STATUS_OK)
                    {
                        console_write(" param0_elem=");
                        console_write_hex64((uint64_t)parameter_type.element_type);
                        console_write(" param0_flags=");
                        console_write_hex64((uint64_t)parameter_type.flags);
                        console_write(" param0_token=");
                        console_write_hex64((uint64_t)parameter_type.type_token.raw);
                    }
                    console_write(" sig_size=");
                    console_write_dec((uint64_t)method->signature.blob.size);
                    console_write(" sig_b0=");
                    console_write_hex64((uint64_t)(method->signature.blob.size > 0u ? method->signature.blob.data[0] : 0u));
                    console_write(" sig_b1=");
                    console_write_hex64((uint64_t)(method->signature.blob.size > 1u ? method->signature.blob.data[1] : 0u));
                    console_write(" sig_b2=");
                    console_write_hex64((uint64_t)(method->signature.blob.size > 2u ? method->signature.blob.data[2] : 0u));
                    console_write(" sig_b3=");
                    console_write_hex64((uint64_t)(method->signature.blob.size > 3u ? method->signature.blob.data[3] : 0u));
                    if (zaclr_metadata_reader_get_methoddef_row(&frame->assembly->metadata,
                                                                resolved_row,
                                                                &debug_row).status == ZACLR_STATUS_OK)
                    {
                        console_write(" row_rva=");
                        console_write_hex64((uint64_t)debug_row.rva);
                        console_write(" row_name_index=");
                        console_write_dec((uint64_t)debug_row.name_index);
                        console_write(" row_sig_index=");
                        console_write_dec((uint64_t)debug_row.signature_blob_index);
                    }
                    console_write("\n");
                    for (uint32_t neighbor = resolved_row > 2u ? (resolved_row - 2u) : 1u;
                         neighbor <= (resolved_row + 2u) && neighbor <= frame->assembly->method_map.count;
                         ++neighbor)
                    {
                        const struct zaclr_method_desc* candidate = zaclr_method_map_find_by_token(&frame->assembly->method_map,
                                                                                                   zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_METHOD << 24) | neighbor));
                        console_write("[ZACLR][call] neighbor_row=");
                        console_write_dec((uint64_t)neighbor);
                        console_write(" name=");
                        console_write(candidate != NULL && candidate->name.text != NULL ? candidate->name.text : "<null>");
                        if (candidate != NULL)
                        {
                            const struct zaclr_type_desc* candidate_type = zaclr_type_map_find_by_token(&frame->assembly->type_map,
                                                                                                          candidate->owning_type_token);
                            console_write(" owner=");
                            console_write(candidate_type != NULL && candidate_type->type_namespace.text != NULL ? candidate_type->type_namespace.text : "<null-ns>");
                            console_write(".");
                            console_write(candidate_type != NULL && candidate_type->type_name.text != NULL ? candidate_type->type_name.text : "<null-type>");
                        }
                        console_write(" param_count=");
                        console_write_dec((uint64_t)(candidate != NULL ? candidate->signature.parameter_count : 0u));
                        console_write(" rva=");
                        console_write_hex64((uint64_t)(candidate != NULL ? candidate->rva : 0u));
                        console_write("\n");
                    }
                    for (uint32_t probe = 260u; probe <= 264u && probe <= frame->assembly->method_map.count; ++probe)
                    {
                        const struct zaclr_method_desc* candidate = zaclr_method_map_find_by_token(&frame->assembly->method_map,
                                                                                                   zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_METHOD << 24) | probe));
                        console_write("[ZACLR][call] probe_row=");
                        console_write_dec((uint64_t)probe);
                        console_write(" name=");
                        console_write(candidate != NULL && candidate->name.text != NULL ? candidate->name.text : "<null>");
                        console_write(" owner=");
                        if (candidate != NULL)
                        {
                            const struct zaclr_type_desc* candidate_type = zaclr_type_map_find_by_token(&frame->assembly->type_map,
                                                                                                          candidate->owning_type_token);
                            console_write(candidate_type != NULL && candidate_type->type_namespace.text != NULL ? candidate_type->type_namespace.text : "<null-ns>");
                            console_write(".");
                            console_write(candidate_type != NULL && candidate_type->type_name.text != NULL ? candidate_type->type_name.text : "<null-type>");
                        }
                        console_write(" param_count=");
                        console_write_dec((uint64_t)(candidate != NULL ? candidate->signature.parameter_count : 0u));
                        console_write("\n");
                    }
                }

                if (method_is_type_initializer_trigger(method))
                {
                    result = ensure_type_initializer_ran(context->runtime,
                                                         frame->assembly,
                                                         zaclr_type_map_find_by_token(&frame->assembly->type_map,
                                                                                      method->owning_type_token));
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }
                }

                if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_METHODSPEC))
                {
                    ZACLR_TRACE_VALUE(context->runtime,
                                      ZACLR_TRACE_CATEGORY_EXEC,
                                      ZACLR_TRACE_EVENT_CALL_TARGET,
                                      method->name.text != NULL ? method->name.text : "<null-method-name>",
                                      (uint64_t)method->token.raw);
                    if (method->token.raw == 0x06008106u)
                    {
                        console_write("[ZACLR][call] ObjectHandleOnStack.Create parent_depth=");
                        console_write_dec((uint64_t)frame->eval_stack.depth);
                        console_write(" il_offset=");
                        console_write_dec((uint64_t)frame->il_offset);
                        console_write("\n");
                    }
                }

                {
                    const struct zaclr_type_desc* owning_type = zaclr_type_map_find_by_token(&frame->assembly->type_map, method->owning_type_token);
                intrinsic_result = try_invoke_intrinsic(frame,
                                                        frame->assembly,
                                                        owning_type,
                                                        method);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "Call.MethodDef.IntrinsicStatus",
                                  (uint64_t)intrinsic_result.status);
                if (intrinsic_result.status == ZACLR_STATUS_OK)
                {
                    return intrinsic_result;
                }
}

                /*
                 * Virtual dispatch: for callvirt, resolve the override
                 * through the runtime object's vtable BEFORE dispatch
                 * classification.  Abstract base methods (rva=0) would
                 * otherwise be classified as NOT_IMPLEMENTED before the
                 * concrete override is found.
                 */
                if (opcode == CEE_CALLVIRT
                    && (method->method_flags & METHOD_FLAG_VIRTUAL) != 0u
                    && frame->eval_stack.depth > 0u)
                {
                    uint32_t this_index = frame->eval_stack.depth - method->signature.parameter_count - 1u;
                    if (this_index < frame->eval_stack.depth)
                    {
                        const struct zaclr_stack_value* this_val = &frame->eval_stack.values[this_index];
                        if (this_val->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE
                            && this_val->data.object_reference != NULL)
                        {
                            const struct zaclr_method_table* runtime_mt =
                                this_val->data.object_reference->header.method_table;
                            if (runtime_mt != NULL)
                            {
                                const struct zaclr_method_desc* override =
                                    zaclr_method_table_resolve_virtual(runtime_mt, method);
                                if (override != NULL && override != method)
                                {
                                    method = override;
                                    if (runtime_mt->assembly != NULL)
                                    {
                                        target_assembly = (struct zaclr_loaded_assembly*)runtime_mt->assembly;
                                    }
                                }
                            }
                        }
                    }
                }

                /*
                 * Exact native binding still runs on resolved bodyless methods.
                 * Do not require impl_flags here as the primary gate; the resolved
                 * method body state plus exact runtime bind decides the outcome.
                 */
                {
                    struct zaclr_method_dispatch_info dispatch_info = {};
                    struct zaclr_result dispatch_result = zaclr_classify_method_dispatch(method, &dispatch_info);

                    if (dispatch_result.status != ZACLR_STATUS_OK)
                    {
                        return dispatch_result;
                    }

                    if (dispatch_info.kind == ZACLR_DISPATCH_KIND_INTERNAL_CALL)
                    {
                        const struct zaclr_type_desc* owning_type = zaclr_type_map_find_by_token(&frame->assembly->type_map,
                                                                                                  method->owning_type_token);
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

                    if (dispatch_info.kind == ZACLR_DISPATCH_KIND_QCALL)
                    {
                        console_write("[ZACLR][qcall] method=");
                        console_write(method->name.text != NULL ? method->name.text : "<null>");
                        console_write(" import=");
                        console_write(dispatch_info.pinvoke_import != NULL ? dispatch_info.pinvoke_import : "<null>");
                        console_write(" module=");
                        console_write(dispatch_info.pinvoke_module != NULL ? dispatch_info.pinvoke_module : "<null>");
                        console_write(" entry=");
                        console_write(dispatch_info.qcall_entry_point != NULL ? dispatch_info.qcall_entry_point : "<null>");
                        console_write("\n");

                        zaclr_native_frame_handler handler = zaclr_qcall_table_resolve(&context->runtime->qcall_table,
                                                                                       dispatch_info.qcall_entry_point);
                        if (handler == NULL)
                        {
                            console_write("[ZACLR][qcall] resolve miss entry=");
                            console_write(dispatch_info.qcall_entry_point != NULL ? dispatch_info.qcall_entry_point : "<null>");
                            console_write("\n");
                            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_INTEROP);
                        }

                        return zaclr_invoke_native_frame_handler_exact(context,
                                                                       frame,
                                                                       frame->assembly,
                                                                       method,
                                                                       handler,
                                                                       ZACLR_NATIVE_CALL_INVOCATION_CALL);
                    }

                    if (dispatch_info.kind == ZACLR_DISPATCH_KIND_PINVOKE
                        || dispatch_info.kind == ZACLR_DISPATCH_KIND_NOT_IMPLEMENTED)
                    {
                        console_write("[ZACLR][dispatch] unimplemented method=");
                        console_write(method->name.text != NULL ? method->name.text : "<null>");
                        console_write(" token=");
                        console_write_hex64((uint64_t)method->token.raw);
                        console_write(" kind=");
                        console_write_dec((uint64_t)dispatch_info.kind);
                        console_write("\n");
                        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_INTEROP);
                    }
                }

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

                intrinsic_result = try_invoke_intrinsic(frame,
                                                        frame->assembly,
                                                        zaclr_type_map_find_by_token(&frame->assembly->type_map, method->owning_type_token),
                                                        method);
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "Call.MethodDef.IntrinsicStatus",
                                  (uint64_t)intrinsic_result.status);
                if (intrinsic_result.status == ZACLR_STATUS_OK)
                {
                    return intrinsic_result;
                }

                result = zaclr_frame_create_child(context->engine,
                                                  context->runtime,
                                                  frame,
                                                  target_assembly != NULL ? target_assembly : frame->assembly,
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

                        if (has_methodspec_instantiation != 0u)
                        {
                            struct zaclr_generic_context methodspec_context = {};
                            struct zaclr_generic_context concrete_method_context = {};

                            result = zaclr_generic_context_set_method_instantiation(&methodspec_context,
                                                                                    frame->assembly,
                                                                                    context->runtime,
                                                                                    &methodspec_instantiation_blob);
                            if (result.status == ZACLR_STATUS_OK)
                            {
                                result = zaclr_generic_context_substitute_method_args(&concrete_method_context,
                                                                                      &methodspec_context,
                                                                                      &frame->generic_context);
                            }
                            if (result.status == ZACLR_STATUS_OK)
                            {
                                result = zaclr_generic_context_assign_method_args(&child->generic_context,
                                                                                  &concrete_method_context);
                            }

                            zaclr_generic_context_reset(&concrete_method_context);
                            zaclr_generic_context_reset(&methodspec_context);
                            if (result.status != ZACLR_STATUS_OK)
                            {
                                zaclr_frame_destroy(child);
                                return result.status == ZACLR_STATUS_NOT_IMPLEMENTED
                                    ? zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC)
                                    : result;
                            }
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
                        struct zaclr_call_target target = {};
                        struct zaclr_frame* child;
                        struct zaclr_result result;

                        if (is_system_object_ctor(&memberref))
                        {
                            return zaclr_result_ok();
                        }

                        result = zaclr_call_target_resolve_memberref(context->runtime,
                                                                    frame->assembly,
                                                                    &memberref,
                                                                    has_methodspec_instantiation != 0u ? &methodspec_instantiation_blob : NULL,
                                                                    &target);
                        if (result.status != ZACLR_STATUS_OK)
                        {
                            return result;
                        }

                        console_write("[ZACLR][call] resolved ptrs asm=");
                        console_write_hex64((uint64_t)(uintptr_t)target.assembly);
                        console_write(" type=");
                        console_write_hex64((uint64_t)(uintptr_t)target.owning_type);
                        console_write(" method=");
                        console_write_hex64((uint64_t)(uintptr_t)target.method);
                        console_write("\n");

                        if (method_is_type_initializer_trigger(target.method))
                        {
                            console_write("[ZACLR][call] before cctor asm_ptr=");
                            console_write_hex64((uint64_t)(uintptr_t)target.assembly);
                            console_write(" method_ptr=");
                            console_write_hex64((uint64_t)(uintptr_t)target.method);
                            console_write("\n");

                            /* When the owning type is a generic instantiation, substitute the
                               caller frame's generic context into the TypeSpec's type_args so
                               the .cctor frame can resolve bare VAR(n)/MVAR(n) tokens. */
                            if (target.has_owning_typespec != 0u
                                && target.owning_typespec.generic_context.type_arg_count > 0u)
                            {
                                struct zaclr_generic_context concrete_type_context = {};

                                console_write("[ZACLR][subst] frame.type_arg_count=");
                                console_write_dec((uint64_t)frame->generic_context.type_arg_count);
                                console_write(" frame.method_arg_count=");
                                console_write_dec((uint64_t)frame->generic_context.method_arg_count);
                                if (frame->generic_context.type_arg_count > 0u && frame->generic_context.type_args != NULL)
                                {
                                    console_write(" frame.type_args[0].kind=");
                                    console_write_dec((uint64_t)frame->generic_context.type_args[0].kind);
                                }
                                if (frame->generic_context.method_arg_count > 0u && frame->generic_context.method_args != NULL)
                                {
                                    console_write(" frame.method_args[0].kind=");
                                    console_write_dec((uint64_t)frame->generic_context.method_args[0].kind);
                                    console_write(" elem_type=0x");
                                    console_write_hex64((uint64_t)frame->generic_context.method_args[0].element_type);
                                }
                                console_write("\n");

                                result = zaclr_generic_context_substitute_type_args(
                                    &concrete_type_context,
                                    &target.owning_typespec.generic_context,
                                    &frame->generic_context);

                                if (result.status == ZACLR_STATUS_OK && concrete_type_context.type_arg_count > 0u && concrete_type_context.type_args != NULL)
                                {
                                    console_write("[ZACLR][subst] result.type_args[0].kind=");
                                    console_write_dec((uint64_t)concrete_type_context.type_args[0].kind);
                                    console_write(" elem_type=0x");
                                    console_write_hex64((uint64_t)concrete_type_context.type_args[0].element_type);
                                    console_write("\n");
                                }

                                if (result.status == ZACLR_STATUS_OK)
                                {
                                    result = zaclr_ensure_type_initializer_ran_with_context(
                                        context->runtime,
                                        target.assembly,
                                        target.owning_type,
                                        &concrete_type_context);
                                }

                                zaclr_generic_context_reset(&concrete_type_context);
                            }
                            else
                            {
                                result = ensure_type_initializer_ran(context->runtime,
                                                                     target.assembly,
                                                                     target.owning_type);
                            }

                            if (result.status != ZACLR_STATUS_OK)
                            {
                                zaclr_call_target_reset(&target);
                                return result;
                            }

                            console_write("[ZACLR][call] after cctor asm_ptr=");
                            console_write_hex64((uint64_t)(uintptr_t)target.assembly);
                            console_write(" method_ptr=");
                            console_write_hex64((uint64_t)(uintptr_t)target.method);
                            console_write(" registry_entries=");
                            console_write_hex64((uint64_t)(uintptr_t)context->runtime->assemblies.entries);
                            console_write(" count=");
                            console_write_dec((uint64_t)context->runtime->assemblies.count);
                            console_write("\n");
                        }

                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_EXEC,
                                          ZACLR_TRACE_EVENT_CALL_TARGET,
                                          target.assembly->assembly_name.text,
                                          (uint64_t)target.assembly->id);

                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_INTEROP,
                                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                          "ResolvedMemberRefMethod",
                                          (uint64_t)target.method->token.raw);
                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_INTEROP,
                                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                                          target.method->name.text,
                                          (uint64_t)target.method->impl_flags);

                        intrinsic_result = try_invoke_intrinsic(frame,
                                                                target.assembly,
                                                                target.owning_type,
                                                                target.method);
                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_EXEC,
                                          ZACLR_TRACE_EVENT_CALL_TARGET,
                                          "Call.MemberRef.IntrinsicStatus",
                                          (uint64_t)intrinsic_result.status);
                        if (intrinsic_result.status == ZACLR_STATUS_OK)
                        {
                            zaclr_call_target_reset(&target);
                            return intrinsic_result;
                        }

                        {
                            struct zaclr_method_dispatch_info dispatch_info = {};
                            struct zaclr_result dispatch_result = zaclr_classify_method_dispatch(target.method, &dispatch_info);

                            if (dispatch_result.status != ZACLR_STATUS_OK)
                            {
                                zaclr_call_target_reset(&target);
                                return dispatch_result;
                            }

                            if (dispatch_info.kind == ZACLR_DISPATCH_KIND_INTERNAL_CALL)
                            {
                                struct zaclr_result call_result = invoke_internal_call_exact(context,
                                                                                            frame,
                                                                                            target.assembly,
                                                                                            target.owning_type,
                                                                                            target.method,
                                                                                            ZACLR_NATIVE_CALL_INVOCATION_CALL);
                                zaclr_call_target_reset(&target);
                                return call_result;
                            }

                            if (dispatch_info.kind == ZACLR_DISPATCH_KIND_QCALL)
                            {
                                zaclr_native_frame_handler handler = zaclr_qcall_table_resolve(&context->runtime->qcall_table,
                                                                                               dispatch_info.qcall_entry_point);
                                if (handler == NULL)
                                {
                                    zaclr_call_target_reset(&target);
                                    return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_INTEROP);
                                }

                                {
                                    struct zaclr_result call_result = zaclr_invoke_native_frame_handler_exact(context,
                                                                                                               frame,
                                                                                                               target.assembly,
                                                                                                               target.method,
                                                                                                               handler,
                                                                                                               ZACLR_NATIVE_CALL_INVOCATION_CALL);
                                    zaclr_call_target_reset(&target);
                                    return call_result;
                                }
                            }

                            if (dispatch_info.kind == ZACLR_DISPATCH_KIND_PINVOKE
                                || dispatch_info.kind == ZACLR_DISPATCH_KIND_NOT_IMPLEMENTED)
                            {
                                zaclr_call_target_reset(&target);
                                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_INTEROP);
                            }
                        }

                        /* Same rule for resolved cross-assembly bodyless methods. */
                        if (target.method->rva == 0u)
                        {
                            struct zaclr_result call_result = invoke_internal_call_exact(context,
                                                                                        frame,
                                                                                        target.assembly,
                                                                                        target.owning_type,
                                                                                        target.method,
                                                                                        ZACLR_NATIVE_CALL_INVOCATION_CALL);
                            zaclr_call_target_reset(&target);
                            return call_result;
                        }

                        result = zaclr_frame_create_child(context->engine,
                                                          context->runtime,
                                                          frame,
                                                          target.assembly,
                                                          target.method,
                                                          &child);
                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_EXEC,
                                          ZACLR_TRACE_EVENT_CALL_TARGET,
                                          "Call.MemberRef.CreateChildStatus",
                                          (uint64_t)result.status);
                        if (result.status != ZACLR_STATUS_OK)
                        {
                            zaclr_call_target_reset(&target);
                            return result;
                        }

                        if (target.has_method_instantiation != 0u)
                        {
                            struct zaclr_generic_context concrete_method_context = {};

                            result = zaclr_generic_context_substitute_method_args(&concrete_method_context,
                                                                                  &target.method_generic_context,
                                                                                  &frame->generic_context);
                            if (result.status == ZACLR_STATUS_OK)
                            {
                                result = zaclr_generic_context_assign_method_args(&child->generic_context,
                                                                                  &concrete_method_context);
                            }

                            zaclr_generic_context_reset(&concrete_method_context);
                            if (result.status != ZACLR_STATUS_OK)
                            {
                                zaclr_frame_destroy(child);
                                zaclr_call_target_reset(&target);
                                return result;
                            }
                        }

                        if (target.has_owning_typespec != 0u)
                        {
                            struct zaclr_generic_context concrete_type_context = {};
                            struct zaclr_result type_context_result = zaclr_generic_context_substitute_type_args(
                                &concrete_type_context,
                                &target.owning_typespec.generic_context,
                                &frame->generic_context);
                            if (type_context_result.status == ZACLR_STATUS_OK)
                            {
                                type_context_result = zaclr_generic_context_assign_type_args(&child->generic_context,
                                                                                             &concrete_type_context);
                            }

                            zaclr_generic_context_reset(&concrete_type_context);
                            if (type_context_result.status != ZACLR_STATUS_OK)
                            {
                                zaclr_frame_destroy(child);
                                zaclr_call_target_reset(&target);
                                return type_context_result;
                            }
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
                            zaclr_call_target_reset(&target);
                            return result;
                        }

                        zaclr_call_target_reset(&target);
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

                result = ensure_type_initializer_ran(context->runtime,
                                                     frame->assembly,
                                                     zaclr_type_map_find_by_token(&frame->assembly->type_map,
                                                                                  method->owning_type_token));
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
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
                    const struct zaclr_type_desc* owning_type = zaclr_type_map_find_by_token(&frame->assembly->type_map,
                                                                                              method->owning_type_token);
                    struct zaclr_method_table* owning_method_table = NULL;
                    bool is_delegate_ctor = false;

                    if (owning_type != NULL)
                    {
                        struct zaclr_result prepare_result = zaclr_type_prepare(frame->runtime,
                                                                              (struct zaclr_loaded_assembly*)frame->assembly,
                                                                              owning_type,
                                                                              &owning_method_table);
                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_EXEC,
                                          ZACLR_TRACE_EVENT_CALL_TARGET,
                                          "NewObj.Delegate.PrepareStatus",
                                          (uint64_t)prepare_result.status);
                        if (prepare_result.status == ZACLR_STATUS_OK
                            && owning_method_table != NULL
                            && zaclr_method_table_is_delegate(owning_method_table) != 0u)
                        {
                            is_delegate_ctor = true;
                        }
                    }

                    ZACLR_TRACE_VALUE(context->runtime,
                                      ZACLR_TRACE_CATEGORY_EXEC,
                                      ZACLR_TRACE_EVENT_CALL_TARGET,
                                      "NewObj.Delegate.IsDelegateCtor",
                                      (uint64_t)(is_delegate_ctor ? 1u : 0u));

                    if (result.status == ZACLR_STATUS_NOT_FOUND
                        && method->rva == 0u
                        && method->name.text != NULL
                        && text_equals(method->name.text, ".ctor")
                        && owning_type != NULL
                        && is_delegate_ctor)
                    {
                        struct zaclr_stack_value method_ptr_value = {};
                        struct zaclr_stack_value target_value = {};
                        struct zaclr_object_desc* delegate_object;
                        struct zaclr_method_handle method_handle = {};
                        uintptr_t packed_method_handle = 0u;
                        struct zaclr_result pop_result;

                        if (method->signature.parameter_count < 2u)
                        {
                            return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
                        }

                        pop_result = zaclr_eval_stack_pop(&frame->eval_stack, &method_ptr_value);
                        if (pop_result.status != ZACLR_STATUS_OK)
                        {
                            return pop_result;
                        }

                        pop_result = zaclr_eval_stack_pop(&frame->eval_stack, &target_value);
                        if (pop_result.status != ZACLR_STATUS_OK)
                        {
                            return pop_result;
                        }

                        if (method_ptr_value.kind == ZACLR_STACK_VALUE_I8)
                        {
                            packed_method_handle = (uintptr_t)method_ptr_value.data.i8;
                        }
                        else if (method_ptr_value.kind == ZACLR_STACK_VALUE_I4)
                        {
                            packed_method_handle = (uintptr_t)(uint32_t)method_ptr_value.data.i4;
                        }
                        else
                        {
                            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
                        }

                        pop_result = zaclr_method_handle_unpack(packed_method_handle, &method_handle);
                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_EXEC,
                                          ZACLR_TRACE_EVENT_CALL_TARGET,
                                          "NewObj.Delegate.UnpackStatus",
                                          (uint64_t)pop_result.status);
                        if (pop_result.status != ZACLR_STATUS_OK)
                        {
                            return pop_result;
                        }

                        delegate_object = zaclr_heap_get_object(&frame->runtime->heap, instance_handle);
                        if (delegate_object == NULL)
                        {
                            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
                        }

                        pop_result = zaclr_delegate_runtime_bind_singlecast(frame->runtime,
                                                                            delegate_object,
                                                                            frame->assembly,
                                                                            owning_type,
                                                                            &target_value,
                                                                            &method_handle);
                        ZACLR_TRACE_VALUE(context->runtime,
                                          ZACLR_TRACE_CATEGORY_EXEC,
                                          ZACLR_TRACE_EVENT_CALL_TARGET,
                                          "NewObj.Delegate.BindSinglecastStatus",
                                          (uint64_t)pop_result.status);
                        if (pop_result.status != ZACLR_STATUS_OK)
                        {
                            return pop_result;
                        }

                        return push_object_handle(frame, instance_handle);
                    }

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

                child->flags |= k_zaclr_frame_flag_newobj_ctor;

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

                console_write("[ZACLR][call] memberref resolve begin assembly=");
                console_write(memberref.assembly_name != NULL ? memberref.assembly_name : "<null>");
                console_write(" type=");
                console_write(memberref.key.type_name != NULL ? memberref.key.type_name : "<null>");
                console_write(" method=");
                console_write(memberref.key.method_name != NULL ? memberref.key.method_name : "<null>");
                console_write("\n");

                result = zaclr_member_resolution_resolve_method(frame->runtime,
                                                                frame->assembly,
                                                                &memberref,
                                                                &target_assembly,
                                                                &target_type,
                                                                &target_method);
                if (result.status != ZACLR_STATUS_OK)
                {
                    console_write("[ZACLR][call] memberref resolve failed\n");
                    return result;
                }

                console_write("[ZACLR][call] memberref resolve ok assembly=");
                console_write(target_assembly != NULL && target_assembly->assembly_name.text != NULL ? target_assembly->assembly_name.text : "<null>");
                console_write(" type=");
                console_write(target_type != NULL && target_type->type_name.text != NULL ? target_type->type_name.text : "<null>");
                console_write(" method=");
                console_write(target_method != NULL && target_method->name.text != NULL ? target_method->name.text : "<null>");
                console_write("\n");

                result = ensure_type_initializer_ran(context->runtime,
                                                     target_assembly,
                                                     target_type);
                if (result.status != ZACLR_STATUS_OK)
                {
                    console_write("[ZACLR][call] ensure_type_initializer_ran failed\n");
                    return result;
                }

                console_write("[ZACLR][call] after ensure_type_initializer_ran\n");

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

                {
                    struct zaclr_method_dispatch_info dispatch_info = {};
                    struct zaclr_result dispatch_result = zaclr_classify_method_dispatch(target_method, &dispatch_info);

                    if (dispatch_result.status != ZACLR_STATUS_OK)
                    {
                        return dispatch_result;
                    }

                    if (dispatch_info.kind == ZACLR_DISPATCH_KIND_INTERNAL_CALL)
                    {
                        return invoke_internal_call_exact(context,
                                                          frame,
                                                          target_assembly,
                                                          target_type,
                                                          target_method,
                                                          ZACLR_NATIVE_CALL_INVOCATION_NEWOBJ);
                    }

                    if (dispatch_info.kind == ZACLR_DISPATCH_KIND_QCALL)
                    {
                        zaclr_native_frame_handler handler = zaclr_qcall_table_resolve(&context->runtime->qcall_table,
                                                                                       dispatch_info.qcall_entry_point);
                        if (handler == NULL)
                        {
                            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_INTEROP);
                        }

                        return zaclr_invoke_native_frame_handler_exact(context,
                                                                       frame,
                                                                       target_assembly,
                                                                       target_method,
                                                                       handler,
                                                                       ZACLR_NATIVE_CALL_INVOCATION_NEWOBJ);
                    }

                    if (dispatch_info.kind == ZACLR_DISPATCH_KIND_PINVOKE
                        || dispatch_info.kind == ZACLR_DISPATCH_KIND_NOT_IMPLEMENTED)
                    {
                        console_write("[ZACLR][dispatch] unimplemented method=");
                        console_write(target_method->name.text != NULL ? target_method->name.text : "<null>");
                        console_write(" token=");
                        console_write_hex64((uint64_t)target_method->token.raw);
                        console_write(" kind=");
                        console_write_dec((uint64_t)dispatch_info.kind);
                        console_write("\n");
                        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_INTEROP);
                    }
                }

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

                child->flags |= k_zaclr_frame_flag_newobj_ctor;

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
            if (value.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
            {
                return zaclr_eval_stack_push(&frame->eval_stack, &value);
            }

            if (value.kind == ZACLR_STACK_VALUE_BYREF)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

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
            result = stack_value_to_object_handle(frame->runtime, &value, &handle);
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

        case CEE_UNBOX:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            struct zaclr_stack_value value = {};
            zaclr_object_handle handle = 0u;
            const struct zaclr_boxed_value_desc* boxed_value;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 5u;
            result = stack_value_to_object_handle(frame->runtime, &value, &handle);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            boxed_value = zaclr_boxed_value_from_handle_const(&frame->runtime->heap, handle);
            if (boxed_value == NULL || boxed_value->type_token_raw != token.raw)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            {
                struct zaclr_stack_value address_value = {};
                result = zaclr_stack_value_set_byref(&address_value,
                                                    (void*)&((struct zaclr_boxed_value_desc*)boxed_value)->value,
                                                    sizeof(struct zaclr_stack_value),
                                                    token.raw,
                                                    ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                return zaclr_eval_stack_push(&frame->eval_stack, &address_value);
            }
        }

        case CEE_STOBJ:
        {
            struct zaclr_stack_value value = {};
            struct zaclr_stack_value address = {};
            struct zaclr_resolved_byref target = {};
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_eval_stack_pop(&frame->eval_stack, &address);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            console_write("[ZACLR][stobj] address kind=");
            console_write_dec((uint64_t)address.kind);
            console_write(" flags=");
            console_write_hex64((uint64_t)address.flags);
            console_write(" payload=");
            console_write_dec((uint64_t)address.payload_size);
            console_write(" raw=");
            console_write_hex64((uint64_t)address.data.raw);
            console_write(" type_token=");
            console_write_hex64((uint64_t)address.type_token_raw);
            console_write("\n");

            result = resolve_byref_target(frame, &address, &target);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 5u;
            if (target.stack_slot != NULL)
            {
                return zaclr_stack_value_assign(target.stack_slot, &value);
            }

            if (target.address == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            if (value.kind == ZACLR_STACK_VALUE_VALUETYPE)
            {
                if (value.payload_size > target.payload_size)
                {
                    return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_EXEC);
                }

                kernel_memcpy(target.address, zaclr_stack_value_payload_const(&value), value.payload_size);
                return zaclr_result_ok();
            }

            if (value.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
            {
                zaclr_object_handle handle = zaclr_heap_get_object_handle(&frame->runtime->heap, value.data.object_reference);
                kernel_memcpy(target.address, &handle, sizeof(handle));
                return zaclr_result_ok();
            }

            if (value.kind == ZACLR_STACK_VALUE_I4)
            {
                kernel_memcpy(target.address, &value.data.i4, target.payload_size < sizeof(value.data.i4) ? target.payload_size : sizeof(value.data.i4));
                return zaclr_result_ok();
            }

            if (value.kind == ZACLR_STACK_VALUE_I8)
            {
                kernel_memcpy(target.address, &value.data.i8, target.payload_size < sizeof(value.data.i8) ? target.payload_size : sizeof(value.data.i8));
                return zaclr_result_ok();
            }

            console_write("[ZACLR][stobj] unhandled value kind=");
            console_write_dec((uint64_t)value.kind);
            console_write(" flags=");
            console_write_hex64((uint64_t)value.flags);
            console_write(" payload=");
            console_write_dec((uint64_t)value.payload_size);
            console_write(" type_token=");
            console_write_hex64((uint64_t)value.type_token_raw);
            console_write(" raw=");
            console_write_hex64((uint64_t)value.data.raw);
            console_write(" target_payload=");
            console_write_dec((uint64_t)target.payload_size);
            console_write(" target_type=");
            console_write_hex64((uint64_t)target.type_token_raw);
            console_write("\n");
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
        }

        case CEE_STIND_I1:
        {
            struct zaclr_stack_value value = {};
            struct zaclr_stack_value address = {};
            struct zaclr_stack_value* target;
            int32_t value_i4;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_eval_stack_pop(&frame->eval_stack, &address);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (address.kind != ZACLR_STACK_VALUE_LOCAL_ADDRESS && address.kind != ZACLR_STACK_VALUE_BYREF)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            target = resolve_local_address_target(frame, &address);
            if (target == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            result = stack_value_to_i32(&value, &value_i4);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 1u;
            target->kind = ZACLR_STACK_VALUE_I4;
            target->data.i4 = (int32_t)(int8_t)value_i4;
            return zaclr_result_ok();
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
            result = stack_value_to_object_handle(frame->runtime, &value, &handle);
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
            result = stack_value_to_object_handle(frame->runtime, &value, &handle);
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
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 2u));
            struct zaclr_stack_value address_value = {};
            struct zaclr_resolved_byref target = {};
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &address_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = resolve_byref_target(frame, &address_value, &target);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 6u;

            if (target.stack_slot != NULL)
            {
                uint8_t zero_bytes[ZACLR_STACK_VALUE_INLINE_BUFFER_BYTES] = {};
                if (target.payload_size == sizeof(struct zaclr_stack_value)
                    && (target.stack_slot->kind == ZACLR_STACK_VALUE_EMPTY || stack_value_kind_is_scalar(target.stack_slot->kind)))
                {
                    zaclr_stack_value_reset(target.stack_slot);
                    return zaclr_result_ok();
                }

                return zaclr_stack_value_set_valuetype(target.stack_slot,
                                                       token.raw,
                                                       zero_bytes,
                                                       0u);
            }

            if (target.address == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            kernel_memset(target.address, 0, target.payload_size);
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
            if (frame->method != NULL && frame->method->token.raw == 0x06000A4Bu)
            {
                console_write("[ZACLR][exec] Type.cctor stsfld token=");
                console_write_hex64((uint64_t)token.raw);
                console_write(" depth_before=");
                console_write_dec((uint64_t)frame->eval_stack.depth);
                console_write("\n");
            }
            frame->il_offset += 5u;
            {
                struct zaclr_result result = store_static_field(frame, token);
                if (frame->method != NULL && frame->method->token.raw == 0x06000A4Bu)
                {
                    console_write("[ZACLR][exec] Type.cctor stsfld status=");
                    console_write_dec((uint64_t)result.status);
                    console_write(" depth_after=");
                    console_write_dec((uint64_t)frame->eval_stack.depth);
                    console_write("\n");
                }

                return result;
            }
        }

        case CEE_STFLD:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            struct zaclr_memberref_target memberref = {};
            struct zaclr_stack_value field_value;
            struct zaclr_stack_value object_value;
            struct zaclr_object_desc* object;
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

            if (object_value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS || object_value.kind == ZACLR_STACK_VALUE_BYREF)
            {
                struct zaclr_stack_value* target = resolve_local_address_target(frame, &object_value);
                if (target == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }

                if (target->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
                {
                    frame->il_offset += 5u;
                    *target = field_value;
                    return zaclr_result_ok();
                }

                object_value = *target;
            }

            result = stack_value_to_object_reference(frame->runtime, &object_value, &object);
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
            return store_object_field(frame->runtime, object, token, &field_value);
        }

        case CEE_LDFLD:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            struct zaclr_memberref_target memberref = {};
            struct zaclr_stack_value object_value;
            struct zaclr_stack_value field_value;
            struct zaclr_object_desc* object;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &object_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            /*
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
            */

            if (object_value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS || object_value.kind == ZACLR_STACK_VALUE_BYREF)
            {
                struct zaclr_resolved_byref target = {};
                result = resolve_byref_target(frame, &object_value, &target);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                if (target.stack_slot != NULL)
                {
                    if (target.stack_slot->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
                    {
                        frame->il_offset += 5u;
                        return zaclr_eval_stack_push(&frame->eval_stack, target.stack_slot);
                    }

                    object_value = *target.stack_slot;
                }
                else
                {
                    result = load_field_from_resolved_byref_payload(frame, &target, token, &field_value);
                    if (result.status == ZACLR_STATUS_OK)
                    {
                        frame->il_offset += 5u;
                        return zaclr_eval_stack_push(&frame->eval_stack, &field_value);
                    }
                }
                /*
                ZACLR_TRACE_VALUE(context->runtime,
                                  ZACLR_TRACE_CATEGORY_EXEC,
                                  ZACLR_TRACE_EVENT_CALL_TARGET,
                                  "Ldfld.ResolvedLocalKind",
                                  (uint64_t)object_value.kind);
                */
            }

            result = stack_value_to_object_reference(frame->runtime, &object_value, &object);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (frame->method != NULL
                && frame->method->name.text != NULL
                && text_equals(frame->method->name.text, "get_Length"))
            {
                const struct zaclr_object_desc* string_object_ref = object;
                if (frame->argument_count != 0u
                    && frame->arguments != NULL
                    && frame->arguments[0].kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE
                    && frame->arguments[0].data.object_reference != NULL)
                {
                    string_object_ref = frame->arguments[0].data.object_reference;
                }

                const struct zaclr_string_desc* string_object = (const struct zaclr_string_desc*)string_object_ref;
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

            if (frame->method != NULL
                && frame->method->name.text != NULL
                && text_equals(frame->method->name.text, "GetTypeFromHandle"))
            {
                console_write("[ZACLR][ldfld] normal path object_ptr=");
                console_write_hex64((uint64_t)(uintptr_t)object);
                console_write(" token=");
                console_write_hex64((uint64_t)token.raw);
                console_write(" kind=");
                console_write_hex64((uint64_t)object_value.kind);
                console_write(" flags=");
                console_write_hex64((uint64_t)(object != NULL ? zaclr_object_flags(object) : 0u));
                console_write(" family=");
                console_write_hex64((uint64_t)(object != NULL ? zaclr_object_family(object) : 0u));
                console_write(" type_id=");
                console_write_hex64((uint64_t)(object != NULL ? zaclr_object_type_id(object) : 0u));
                console_write("\n");
            }

            frame->il_offset += 5u;
            result = load_object_field(frame->runtime, object, token, &field_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                if (frame->method != NULL
                    && frame->method->name.text != NULL
                    && text_equals(frame->method->name.text, "GetTypeFromHandle"))
                {
                    console_write("[ZACLR][ldfld] normal path status=");
                    console_write_hex64((uint64_t)result.status);
                    console_write(" category=");
                    console_write_hex64((uint64_t)result.category);
                    console_write("\n");
                }
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

            if (object_value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS || object_value.kind == ZACLR_STACK_VALUE_BYREF)
            {
                struct zaclr_stack_value* target = resolve_local_address_target(frame, &object_value);
                if (target == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }

                object_value = *target;
            }

            if (object_value.kind != ZACLR_STACK_VALUE_VALUETYPE && object_value.kind != ZACLR_STACK_VALUE_BYREF)
            {
                result = stack_value_to_object_handle(frame->runtime, &object_value, &handle);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
            }
            else
            {
                handle = 0u;
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
                if (object_value.kind == ZACLR_STACK_VALUE_VALUETYPE || object_value.kind == ZACLR_STACK_VALUE_BYREF)
                {
                    struct zaclr_resolved_byref target = {};
                    struct zaclr_method_table* method_table = NULL;
                    const struct zaclr_field_layout* layout;
                    const uint8_t* field_address;
                    struct zaclr_stack_value address_value = {};
                    struct zaclr_token value_type_token;
                    const struct zaclr_loaded_assembly* type_assembly = NULL;
                    const struct zaclr_type_desc* type_desc = NULL;

                    result = resolve_byref_target(frame, &object_value, &target);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }

                    value_type_token = zaclr_token_make(target.type_token_raw);
                    result = zaclr_type_system_resolve_type_desc(frame->assembly,
                                                                 frame->runtime,
                                                                 value_type_token,
                                                                 &type_assembly,
                                                                 &type_desc);
                    if (result.status != ZACLR_STATUS_OK || type_desc == NULL || type_assembly == NULL)
                    {
                        return result.status == ZACLR_STATUS_OK
                            ? zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC)
                            : result;
                    }

                    result = zaclr_type_prepare(frame->runtime,
                                                (struct zaclr_loaded_assembly*)type_assembly,
                                                type_desc,
                                                &method_table);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }

                    layout = find_instance_field_layout_in_method_table(method_table, token);
                    if (layout == NULL)
                    {
                        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
                    }

                    field_address = target.address + layout->byte_offset;
                    frame->il_offset += 5u;
                    result = zaclr_stack_value_set_byref(&address_value,
                                                         (void*)field_address,
                                                         zaclr_field_layout_size_from_element_type(layout->element_type),
                                                         token.raw,
                                                         ZACLR_STACK_VALUE_FLAG_NONE);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }
                    return zaclr_eval_stack_push(&frame->eval_stack, &address_value);
                }
                else
                {
                    struct zaclr_object_desc* raw_object = zaclr_heap_get_object(&frame->runtime->heap, handle);
                    struct zaclr_reference_object_desc* object = (struct zaclr_reference_object_desc*)raw_object;
                    struct zaclr_stack_value* field_storage;
                    if (raw_object == NULL)
                    {
                        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
                    }

                    if ((zaclr_object_flags(raw_object) & ZACLR_OBJECT_FLAG_STRING) != 0u)
                    {
                        const struct zaclr_string_desc* string_object = (const struct zaclr_string_desc*)raw_object;
                        struct zaclr_stack_value address_value = {};
                        result = zaclr_stack_value_set_byref(&address_value,
                                                             (void*)zaclr_string_chars(string_object),
                                                             sizeof(uint16_t),
                                                             0u,
                                                             ZACLR_STACK_VALUE_FLAG_NONE);
                        if (result.status != ZACLR_STATUS_OK)
                        {
                            return result;
                        }
                        frame->il_offset += 5u;
                        return zaclr_eval_stack_push(&frame->eval_stack, &address_value);
                    }

                    field_storage = zaclr_reference_object_field_storage(object, token);
                    if (field_storage == NULL)
                    {
                        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
                    }

                    frame->il_offset += 5u;
                    {
                        struct zaclr_stack_value address_value = {};
                        result = zaclr_stack_value_set_byref(&address_value,
                                                             field_storage,
                                                             sizeof(struct zaclr_stack_value),
                                                             token.raw,
                                                             ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT);
                        if (result.status != ZACLR_STATUS_OK)
                        {
                            return result;
                        }
                        return zaclr_eval_stack_push(&frame->eval_stack, &address_value);
                    }
                }
            }
        }

        case CEE_LDOBJ:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 2u));
            struct zaclr_stack_value address_value = {};
            struct zaclr_resolved_byref target = {};
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &address_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = resolve_byref_target(frame, &address_value, &target);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 6u;
            if (target.stack_slot != NULL)
            {
                return zaclr_eval_stack_push(&frame->eval_stack, target.stack_slot);
            }

            if (target.payload_size == sizeof(zaclr_object_handle))
            {
                zaclr_object_handle handle = 0u;
                kernel_memcpy(&handle, target.address, sizeof(handle));
                return push_object_handle(frame, handle);
            }

            if (target.payload_size <= 4u)
            {
                uint32_t value_i4 = 0u;
                kernel_memcpy(&value_i4, target.address, target.payload_size);
                return push_i4(frame, (int32_t)value_i4);
            }

            if (target.payload_size == 8u)
            {
                uint64_t value_i8 = 0u;
                kernel_memcpy(&value_i8, target.address, sizeof(value_i8));
                return push_i8(frame, (int64_t)value_i8);
            }

            {
                struct zaclr_stack_value value = {};
                result = zaclr_stack_value_set_valuetype(&value, token.raw, target.address, target.payload_size);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = zaclr_eval_stack_push(&frame->eval_stack, &value);
                zaclr_stack_value_reset(&value);
                return result;
            }
        }

        case CEE_LDSFLDA:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 2u));
            uint32_t field_row = zaclr_token_row(&token);
            struct zaclr_stack_value address_value = {};

            if (frame->method != NULL && frame->method->token.raw == 0x06000A4Bu)
            {
                console_write("[ZACLR][exec] Type.cctor ldsflda token=");
                console_write_hex64((uint64_t)token.raw);
                console_write(" depth_before=");
                console_write_dec((uint64_t)frame->eval_stack.depth);
                console_write("\n");
            }

            if (!zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD)
                || field_row == 0u
                || field_row > frame->assembly->static_field_count
                || frame->assembly->static_fields == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            frame->il_offset += 6u;
            {
                struct zaclr_result result = zaclr_stack_value_set_byref(&address_value,
                                                                         &frame->assembly->static_fields[field_row - 1u],
                                                                         sizeof(struct zaclr_stack_value),
                                                                         token.raw,
                                                                         ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
            }

            {
                struct zaclr_result push_result = zaclr_eval_stack_push(&frame->eval_stack, &address_value);
                if (frame->method != NULL && frame->method->token.raw == 0x06000A4Bu)
                {
                    console_write("[ZACLR][exec] Type.cctor ldsflda push_status=");
                    console_write_dec((uint64_t)push_result.status);
                    console_write(" depth_after=");
                    console_write_dec((uint64_t)frame->eval_stack.depth);
                    console_write("\n");
                }

                return push_result;
            }
        }

        case CEE_LDELEMA:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 2u));
            struct zaclr_stack_value index_value = {};
            struct zaclr_stack_value array_value = {};
            struct zaclr_array_desc* array;
            struct zaclr_object_desc* array_object;
            uint32_t index = 0u;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &index_value);
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

            result = stack_value_to_object_reference(frame->runtime, &array_value, &array_object);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            array = (struct zaclr_array_desc*)array_object;
            if (array == NULL || index >= zaclr_array_length(array))
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            frame->il_offset += 6u;
            {
                struct zaclr_stack_value address_value = {};
                uint8_t* address = (uint8_t*)zaclr_array_data(array) + ((size_t)index * zaclr_array_element_size(array));
                result = zaclr_stack_value_set_byref(&address_value,
                                                     address,
                                                     zaclr_array_element_size(array),
                                                     token.raw != 0u ? token.raw : zaclr_array_element_type(array).raw,
                                                     ZACLR_STACK_VALUE_FLAG_NONE);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                return zaclr_eval_stack_push(&frame->eval_stack, &address_value);
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
            if (frame->method != NULL && frame->method->token.raw == 0x06000A4Bu)
            {
                console_write("[ZACLR][exec] Type.cctor ldloca.s local=");
                console_write_dec((uint64_t)local_index);
                console_write(" depth_before=");
                console_write_dec((uint64_t)frame->eval_stack.depth);
                console_write(" il_offset=");
                console_write_dec((uint64_t)frame->il_offset);
                console_write("\n");
            }
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

        case CEE_LDTOKEN:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 1u));
            const struct zaclr_loaded_assembly* target_assembly = NULL;
            const struct zaclr_type_desc* target_type = NULL;
            struct zaclr_result result;
            zaclr_object_handle runtime_type_handle;
            struct zaclr_runtime_type_desc* runtime_type;
            uint32_t type_row;

            console_write("[ZACLR][ldtoken] method=");
            console_write(frame->method != NULL && frame->method->name.text != NULL ? frame->method->name.text : "<null>");
            console_write(" token_raw=");
            console_write_hex64((uint64_t)token.raw);
            console_write(" type_args=");
            console_write_dec((uint64_t)frame->generic_context.type_arg_count);
            console_write(" method_args=");
            console_write_dec((uint64_t)frame->generic_context.method_arg_count);
            console_write(" il_offset=");
            console_write_dec((uint64_t)frame->il_offset);
            console_write("\n");

            frame->il_offset += 5u;

            if (frame->method != NULL
                && frame->method->name.text != NULL
                && zaclr_text_equals(frame->method->name.text, "TypeHandleOf")
                && frame->generic_context.method_arg_count > 0u)
            {
                const struct zaclr_generic_argument* method_arg = &frame->generic_context.method_args[0];
                const struct zaclr_loaded_assembly* generic_assembly = method_arg->assembly != NULL ? method_arg->assembly : frame->assembly;
                const struct zaclr_type_desc* runtime_type_handle_type;
                struct zaclr_member_name_ref runtime_type_handle_name = { "System", "RuntimeTypeHandle", NULL };
                struct zaclr_stack_value runtime_type_handle_value = {};
                if (generic_assembly != NULL
                    && zaclr_token_matches_table(&method_arg->token, ZACLR_TOKEN_TABLE_TYPEDEF)
                    && generic_assembly->runtime_type_cache != NULL)
                {
                    uint32_t generic_type_row = zaclr_token_row(&method_arg->token);
                    if (generic_type_row != 0u && generic_type_row <= generic_assembly->runtime_type_cache_count)
                    {
                        runtime_type_handle = generic_assembly->runtime_type_cache[generic_type_row - 1u];
                    }
                }

                if (runtime_type_handle == 0u)
                {
                    result = zaclr_runtime_type_allocate_handle(&frame->runtime->heap,
                                                               generic_assembly,
                                                               method_arg->token,
                                                               &runtime_type_handle);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }

                    if (generic_assembly != NULL
                        && zaclr_token_matches_table(&method_arg->token, ZACLR_TOKEN_TABLE_TYPEDEF)
                        && generic_assembly->runtime_type_cache != NULL)
                    {
                        uint32_t generic_type_row = zaclr_token_row(&method_arg->token);
                        if (generic_type_row != 0u && generic_type_row <= generic_assembly->runtime_type_cache_count)
                        {
                            ((struct zaclr_loaded_assembly*)generic_assembly)->runtime_type_cache[generic_type_row - 1u] = runtime_type_handle;
                        }
                    }
                }

                runtime_type_handle_type = zaclr_type_system_find_type_by_name(frame->assembly, &runtime_type_handle_name);
                if (runtime_type_handle_type == NULL)
                {
                    return push_object_handle(frame, runtime_type_handle);
                }

                result = zaclr_stack_value_set_valuetype(&runtime_type_handle_value,
                                                        runtime_type_handle_type->token.raw,
                                                        &runtime_type_handle,
                                                        sizeof(runtime_type_handle));
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                return zaclr_eval_stack_push(&frame->eval_stack, &runtime_type_handle_value);
            }

            result = resolve_ldtoken_target(frame, token, &target_assembly, &target_type);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if (target_assembly == NULL || target_type == NULL || !zaclr_token_matches_table(&target_type->token, ZACLR_TOKEN_TABLE_TYPEDEF))
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            type_row = zaclr_token_row(&target_type->token);
            if (type_row == 0u || type_row > target_assembly->runtime_type_cache_count || target_assembly->runtime_type_cache == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
            }

            runtime_type_handle = target_assembly->runtime_type_cache[type_row - 1u];
            if (runtime_type_handle == 0u)
            {
                result = zaclr_runtime_type_allocate(&frame->runtime->heap,
                                                    target_assembly,
                                                    target_type->token,
                                                    &runtime_type);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                runtime_type_handle = zaclr_heap_get_object_handle(&frame->runtime->heap, &runtime_type->object);
                ((struct zaclr_loaded_assembly*)target_assembly)->runtime_type_cache[type_row - 1u] = runtime_type_handle;
            }

            return push_object_handle(frame, runtime_type_handle);
        }

        case CEE_LDFTN:
        {
            struct zaclr_token token = zaclr_token_make(read_u32(frame->il_start + frame->il_offset + 2u));
            const struct zaclr_method_desc* target_method = NULL;
            struct zaclr_result result = zaclr_result_ok();

            frame->il_offset += 6u;

            if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_METHOD))
            {
                target_method = zaclr_method_map_find_by_token(&frame->assembly->method_map, token);
                if (target_method == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
                }
            }
            else if (zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_MEMBERREF))
            {
                struct zaclr_memberref_target memberref = {};
                const struct zaclr_loaded_assembly* target_assembly = NULL;
                const struct zaclr_type_desc* target_type = NULL;
                result = zaclr_metadata_get_memberref_info(frame->assembly, token, &memberref);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }

                result = zaclr_member_resolution_resolve_method(frame->runtime,
                                                               frame->assembly,
                                                               &memberref,
                                                               &target_assembly,
                                                               &target_type,
                                                               &target_method);
                (void)target_type;
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
            }
            else
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            return push_i8(frame, (int64_t)(uintptr_t)target_method);
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
            struct zaclr_array_desc* array;

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
                                          &array);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            frame->il_offset += 5u;
            return push_object_handle(frame, zaclr_heap_get_object_handle(&frame->runtime->heap, &array->object));
        }

        case CEE_LDLEN:
        {
            struct zaclr_stack_value array_value;
            struct zaclr_array_desc* array;
            struct zaclr_object_desc* array_object;
            struct zaclr_result result = zaclr_eval_stack_pop(&frame->eval_stack, &array_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = stack_value_to_object_reference(frame->runtime, &array_value, &array_object);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            array = (struct zaclr_array_desc*)array_object;
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
            struct zaclr_object_desc* array_object;

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

            result = stack_value_to_object_reference(frame->runtime, &array_value, &array_object);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            array = (struct zaclr_array_desc*)array_object;
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
            struct zaclr_object_desc* array_object;

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

            result = stack_value_to_object_reference(frame->runtime, &array_value, &array_object);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            array = (struct zaclr_array_desc*)array_object;
            if (array == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
            }

            frame->il_offset += opcode == CEE_STELEM ? 5u : 1u;
            return write_array_indexed_value(frame->runtime, array, index, opcode, &element_value);
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
            else if (value.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
            {
                branch = (value.data.object_reference != NULL);
            }
            else if (value.kind == ZACLR_STACK_VALUE_VALUETYPE)
            {
                const void* payload = zaclr_stack_value_payload_const(&value);
                if (payload == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }

                if (value.payload_size <= 4u)
                {
                    uint32_t raw = 0u;
                    kernel_memcpy(&raw, payload, value.payload_size);
                    branch = raw != 0u;
                }
                else if (value.payload_size == 8u)
                {
                    uint64_t raw = 0u;
                    kernel_memcpy(&raw, payload, sizeof(raw));
                    branch = raw != 0u;
                }
                else
                {
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }
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
            else if (value.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
            {
                branch = (value.data.object_reference != NULL);
            }
            else if (value.kind == ZACLR_STACK_VALUE_VALUETYPE)
            {
                const void* payload = zaclr_stack_value_payload_const(&value);
                if (payload == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }

                if (value.payload_size <= 4u)
                {
                    uint32_t raw = 0u;
                    kernel_memcpy(&raw, payload, value.payload_size);
                    branch = raw != 0u;
                }
                else if (value.payload_size == 8u)
                {
                    uint64_t raw = 0u;
                    kernel_memcpy(&raw, payload, sizeof(raw));
                    branch = raw != 0u;
                }
                else
                {
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }
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

            if (frame->method != NULL && frame->method->token.raw == 0x06007897u)
            {
                console_write("[ZACLR][compare] method=get_IsAllocated opcode=");
                console_write_hex64((uint64_t)opcode);
                console_write(" left_kind=");
                console_write_dec((uint64_t)left.kind);
                console_write(" right_kind=");
                console_write_dec((uint64_t)right.kind);
                console_write(" left_raw=");
                console_write_hex64((uint64_t)left.data.raw);
                console_write(" right_raw=");
                console_write_hex64((uint64_t)right.data.raw);
                console_write("\n");
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
                     && (frame->flags & k_zaclr_frame_flag_newobj_ctor) != 0u
                     && frame->argument_count != 0u
                     && frame->arguments != NULL
                     && frame->arguments[0].kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
            {
                const struct zaclr_type_desc* owning_type = frame->method != NULL
                    ? zaclr_type_map_find_by_token(&frame->assembly->type_map, frame->method->owning_type_token)
                    : NULL;
                struct zaclr_method_table* owning_method_table = NULL;
                struct zaclr_result prepare_result = (owning_type != NULL)
                    ? zaclr_type_prepare(frame->runtime,
                                         (struct zaclr_loaded_assembly*)frame->assembly,
                                         owning_type,
                                         &owning_method_table)
                    : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);

                if (prepare_result.status == ZACLR_STATUS_OK
                    && owning_method_table != NULL
                    && zaclr_method_table_is_value_type(owning_method_table) != 0u)
                {
                    const struct zaclr_object_desc* boxed_like_object = frame->arguments[0].data.object_reference;
                    const uint32_t payload_size = owning_method_table->instance_size > ZACLR_OBJECT_HEADER_SIZE
                        ? (owning_method_table->instance_size - ZACLR_OBJECT_HEADER_SIZE)
                        : 0u;
                    const void* payload = boxed_like_object != NULL
                        ? ((const uint8_t*)boxed_like_object + sizeof(struct zaclr_reference_object_desc))
                        : NULL;
                    struct zaclr_stack_value value_result = {};
                    struct zaclr_result result = zaclr_stack_value_set_valuetype(&value_result,
                                                                                 frame->method->owning_type_token.raw,
                                                                                 payload,
                                                                                 payload_size);
                    console_write("[ZACLR][ret-newobj] valuetype method=");
                    console_write(frame->method != NULL && frame->method->name.text != NULL ? frame->method->name.text : "<null>");
                    console_write(" token=");
                    console_write_hex64((uint64_t)(frame->method != NULL ? frame->method->owning_type_token.raw : 0u));
                    console_write(" payload=");
                    console_write_dec((uint64_t)payload_size);
                    console_write(" set_status=");
                    console_write_dec((uint64_t)result.status);
                    console_write("\n");
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        zaclr_stack_value_reset(&value_result);
                        return result;
                    }

                    result = zaclr_eval_stack_push(&parent->eval_stack, &value_result);
                    zaclr_stack_value_reset(&value_result);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }
                }
                else
                {
                    struct zaclr_result result = zaclr_eval_stack_push(&parent->eval_stack, &frame->arguments[0]);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        return result;
                    }
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
