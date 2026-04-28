#include <kernel/zaclr/exec/zaclr_intrinsics.h>

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/exec/zaclr_dispatch_helpers.h>
#include <kernel/zaclr/exec/zaclr_eval_stack.h>
#include <kernel/zaclr/exec/zaclr_frame.h>
#include <kernel/zaclr/heap/zaclr_gc_roots.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_field_layout.h>
#include <kernel/zaclr/typesystem/zaclr_method_table.h>
#include <kernel/zaclr/typesystem/zaclr_type_prepare.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static bool is_memberinfo_get_module_intrinsic(const struct zaclr_type_desc* type,
                                                   const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && zaclr_text_equals(type->type_namespace.text, "System.Reflection")
            && zaclr_text_equals(type->type_name.text, "MemberInfo")
            && zaclr_text_equals(method->name.text, "get_Module")
            && method->signature.parameter_count == 0u
            && (method->signature.calling_convention & 0x20u) != 0u;
    }

    static struct zaclr_result invoke_memberinfo_get_module_intrinsic(struct zaclr_frame* frame)
    {
        struct zaclr_stack_value this_value = {};
        struct zaclr_stack_value module_value = {};

        if (frame == NULL || frame->argument_count == 0u || frame->arguments == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        this_value = frame->arguments[0];
        if (this_value.kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE || this_value.data.object_reference == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        module_value.kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        module_value.data.object_reference = this_value.data.object_reference;
        return zaclr_eval_stack_push(&frame->eval_stack, &module_value);
    }

    /* RuntimeHelpers intrinsics reference map for future ZACLR bring-up:
       - CoreCLR managed declaration + [Intrinsic] marker:
         CLONES/runtime/src/coreclr/System.Private.CoreLib/src/System/Runtime/CompilerServices/RuntimeHelpers.CoreCLR.cs
       - CoreCLR method identity table used by the runtime:
         CLONES/runtime/src/coreclr/vm/corelib.h
       - CoreCLR EE/JIT-side replacement logic:
         CLONES/runtime/src/coreclr/vm/jitinterface.cpp
       - NativeAOT / shared-compiler fallback stub generation:
         CLONES/runtime/src/coreclr/tools/Common/TypeSystem/IL/Stubs/RuntimeHelpersIntrinsics.cs
         CLONES/runtime/src/coreclr/nativeaot/System.Private.CoreLib/src/System/Runtime/CompilerServices/RuntimeHelpers.NativeAot.cs

       RuntimeHelpers methods currently listed in corelib.h that should be reviewed as mandatory runtime
       intrinsics / EE-owned helpers instead of ordinary IL fallback whenever ZACLR reaches them:
       - IsBitwiseEquatable
       - GetRawData
       - GetUninitializedObject
       - EnumEquals
       - EnumCompareTo
       - AllocTailCallArgBuffer
       - DispatchTailCalls
    */

    static bool text_equals(const char* left, const char* right)
    {
        return zaclr_text_equals(left, right);
    }

    enum class zaclr_string_intrinsic_kind : uint8_t
    {
        none = 0,
        starts_with,
        ends_with,
        contains,
        compare,
        to_upper,
        to_lower,
        trim,
        replace,
        index_of_char
    };

    static struct zaclr_stack_value* resolve_local_address_target(struct zaclr_frame* frame,
                                                                  struct zaclr_stack_value* value)
    {
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

        return zaclr_dispatch_resolve_local_address_target(frame, value);
    }

    static struct zaclr_result push_i4(struct zaclr_frame* frame, int32_t value)
    {
        return zaclr_dispatch_push_i4(frame, value);
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

    static struct zaclr_result push_object_reference(struct zaclr_frame* frame,
                                                     struct zaclr_object_desc* value)
    {
        struct zaclr_stack_value stack_value = {};
        stack_value.kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        stack_value.data.object_reference = value;
        return zaclr_eval_stack_push(&frame->eval_stack, &stack_value);
    }

    static uint32_t stack_value_scalar_width(const struct zaclr_stack_value* value)
    {
        if (value == NULL)
        {
            return 0u;
        }

        if (value->kind == ZACLR_STACK_VALUE_I8 || value->kind == ZACLR_STACK_VALUE_R8)
        {
            return 8u;
        }

        if (value->kind == ZACLR_STACK_VALUE_I4 || value->kind == ZACLR_STACK_VALUE_R4)
        {
            return 4u;
        }

        if (value->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        {
            return (uint32_t)sizeof(void*);
        }

        if (value->payload_size != 0u && value->payload_size <= 8u)
        {
            return value->payload_size;
        }

        return 0u;
    }

    static struct zaclr_result raw_address_from_stack_value(struct zaclr_frame* frame,
                                                            const struct zaclr_stack_value* value,
                                                            uintptr_t* out_address,
                                                            uint32_t* out_payload_size,
                                                            uint32_t* out_type_token_raw)
    {
        struct zaclr_stack_value mutable_value = {};
        struct zaclr_stack_value* target;
        void* payload;
        uint32_t width;

        if (out_address == NULL || out_payload_size == NULL || out_type_token_raw == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_address = 0u;
        *out_payload_size = 0u;
        *out_type_token_raw = 0u;
        if (value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (value->kind == ZACLR_STACK_VALUE_BYREF)
        {
            if ((value->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) == 0u)
            {
                *out_address = value->data.raw;
                *out_payload_size = value->payload_size;
                *out_type_token_raw = value->type_token_raw;
                return zaclr_result_ok();
            }

            target = (struct zaclr_stack_value*)(uintptr_t)value->data.raw;
            if (target == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            if (target->kind == ZACLR_STACK_VALUE_BYREF || target->kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
            {
                return raw_address_from_stack_value(frame,
                                                    target,
                                                    out_address,
                                                    out_payload_size,
                                                    out_type_token_raw);
            }

            if (target->kind == ZACLR_STACK_VALUE_VALUETYPE)
            {
                payload = zaclr_stack_value_payload(target);
                if (payload == NULL)
                {
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }

                *out_address = (uintptr_t)payload;
                *out_payload_size = target->payload_size;
                *out_type_token_raw = target->type_token_raw;
                return zaclr_result_ok();
            }

            width = stack_value_scalar_width(target);
            if (width == 0u)
            {
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
            }

            *out_address = (uintptr_t)&target->data.raw;
            *out_payload_size = width;
            *out_type_token_raw = target->type_token_raw;
            return zaclr_result_ok();
        }

        if (value->kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
        {
            target = resolve_local_address_target(frame, (struct zaclr_stack_value*)value);
            if (target == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            if (target->kind == ZACLR_STACK_VALUE_BYREF || target->kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
            {
                return raw_address_from_stack_value(frame,
                                                    target,
                                                    out_address,
                                                    out_payload_size,
                                                    out_type_token_raw);
            }

            mutable_value.kind = ZACLR_STACK_VALUE_BYREF;
            mutable_value.flags = ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT;
            mutable_value.data.raw = (uintptr_t)target;
            return raw_address_from_stack_value(frame,
                                                &mutable_value,
                                                out_address,
                                                out_payload_size,
                                                out_type_token_raw);
        }

        if (value->kind == ZACLR_STACK_VALUE_VALUETYPE)
        {
            payload = zaclr_stack_value_payload((struct zaclr_stack_value*)value);
            if (payload == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            *out_address = (uintptr_t)payload;
            *out_payload_size = value->payload_size;
            *out_type_token_raw = value->type_token_raw;
            return zaclr_result_ok();
        }

        if (value->kind == ZACLR_STACK_VALUE_I8)
        {
            *out_address = (uintptr_t)value->data.i8;
            *out_payload_size = value->payload_size;
            *out_type_token_raw = value->type_token_raw;
            return zaclr_result_ok();
        }

        if (value->kind == ZACLR_STACK_VALUE_I4)
        {
            *out_address = (uintptr_t)(uint32_t)value->data.i4;
            *out_payload_size = value->payload_size;
            *out_type_token_raw = value->type_token_raw;
            return zaclr_result_ok();
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    static const uint8_t* read_payload_from_stack_value(struct zaclr_frame* frame,
                                                        const struct zaclr_stack_value* value,
                                                        uint32_t* out_payload_size,
                                                        uint32_t* out_type_token_raw)
    {
        uintptr_t address = 0u;
        uint32_t payload_size = 0u;
        uint32_t type_token_raw = 0u;
        struct zaclr_result result = raw_address_from_stack_value(frame,
                                                                  value,
                                                                  &address,
                                                                  &payload_size,
                                                                  &type_token_raw);
        if (result.status != ZACLR_STATUS_OK || address == 0u)
        {
            return NULL;
        }

        if (out_payload_size != NULL)
        {
            *out_payload_size = payload_size;
        }

        if (out_type_token_raw != NULL)
        {
            *out_type_token_raw = type_token_raw;
        }

        return (const uint8_t*)address;
    }

    static const struct zaclr_field_layout* find_instance_field_layout_by_name(struct zaclr_runtime* runtime,
                                                                               const struct zaclr_loaded_assembly* assembly,
                                                                               const struct zaclr_type_desc* type,
                                                                               const char* field_name)
    {
        struct zaclr_method_table* method_table = NULL;
        struct zaclr_result result;

        if (runtime == NULL || assembly == NULL || type == NULL || field_name == NULL)
        {
            return NULL;
        }

        result = zaclr_type_prepare(runtime,
                                    (struct zaclr_loaded_assembly*)assembly,
                                    type,
                                    &method_table);
        if (result.status != ZACLR_STATUS_OK || method_table == NULL || method_table->instance_fields == NULL)
        {
            return NULL;
        }

        for (uint32_t index = 0u; index < method_table->instance_field_count; ++index)
        {
            const struct zaclr_field_layout* layout = &method_table->instance_fields[index];
            struct zaclr_field_row field_row = {};
            struct zaclr_name_view name = {};
            if (layout->is_static != 0u)
            {
                continue;
            }

            if (zaclr_metadata_reader_get_field_row(&assembly->metadata,
                                                    layout->field_token_row,
                                                    &field_row).status != ZACLR_STATUS_OK
                || zaclr_metadata_reader_get_string(&assembly->metadata,
                                                    field_row.name_index,
                                                    &name).status != ZACLR_STATUS_OK)
            {
                continue;
            }

            if (text_equals(name.text, field_name))
            {
                return layout;
            }
        }

        return NULL;
    }

    static uint32_t numeric_width_from_element_type(uint8_t element_type)
    {
        switch (element_type)
        {
            case ZACLR_ELEMENT_TYPE_BOOLEAN:
            case ZACLR_ELEMENT_TYPE_I1:
            case ZACLR_ELEMENT_TYPE_U1:
                return 1u;
            case ZACLR_ELEMENT_TYPE_CHAR:
            case ZACLR_ELEMENT_TYPE_I2:
            case ZACLR_ELEMENT_TYPE_U2:
                return 2u;
            case ZACLR_ELEMENT_TYPE_I4:
            case ZACLR_ELEMENT_TYPE_U4:
            case ZACLR_ELEMENT_TYPE_R4:
                return 4u;
            case ZACLR_ELEMENT_TYPE_I:
            case ZACLR_ELEMENT_TYPE_U:
            case ZACLR_ELEMENT_TYPE_I8:
            case ZACLR_ELEMENT_TYPE_U8:
            case ZACLR_ELEMENT_TYPE_R8:
                return 8u;
            default:
                return 0u;
        }
    }

    static uint32_t numeric_width_from_stack_value(const struct zaclr_stack_value* value)
    {
        if (value == NULL)
        {
            return 0u;
        }

        if (value->kind == ZACLR_STACK_VALUE_I8 || value->kind == ZACLR_STACK_VALUE_R8)
        {
            return 8u;
        }

        if (value->kind == ZACLR_STACK_VALUE_I4 || value->kind == ZACLR_STACK_VALUE_R4)
        {
            return 4u;
        }

        if (value->payload_size != 0u && value->payload_size <= 8u)
        {
            return value->payload_size;
        }

        return 0u;
    }

    static bool numeric_element_type_is_unsigned(uint8_t element_type)
    {
        switch (element_type)
        {
            case ZACLR_ELEMENT_TYPE_BOOLEAN:
            case ZACLR_ELEMENT_TYPE_CHAR:
            case ZACLR_ELEMENT_TYPE_U1:
            case ZACLR_ELEMENT_TYPE_U2:
            case ZACLR_ELEMENT_TYPE_U4:
            case ZACLR_ELEMENT_TYPE_U8:
            case ZACLR_ELEMENT_TYPE_U:
                return true;
            default:
                return false;
        }
    }

    static uint64_t numeric_stack_value_bits(const struct zaclr_stack_value* value,
                                             uint32_t width)
    {
        uint64_t bits = 0u;
        const void* payload;

        if (value == NULL)
        {
            return 0u;
        }

        switch (value->kind)
        {
            case ZACLR_STACK_VALUE_I8:
                return (uint64_t)value->data.i8;
            case ZACLR_STACK_VALUE_R8:
                return value->data.r8_bits;
            case ZACLR_STACK_VALUE_R4:
                return (uint64_t)value->data.r4_bits;
            case ZACLR_STACK_VALUE_I4:
                return (uint64_t)(uint32_t)value->data.i4;
            case ZACLR_STACK_VALUE_VALUETYPE:
                payload = zaclr_stack_value_payload_const(value);
                if (payload != NULL)
                {
                    kernel_memcpy(&bits, payload, value->payload_size < width ? value->payload_size : width);
                }
                return bits;
            default:
                return (uint64_t)value->data.raw;
        }
    }

    static int64_t numeric_sign_extend(uint64_t bits,
                                       uint32_t width)
    {
        switch (width)
        {
            case 1u:
                return (int64_t)(int8_t)(uint8_t)bits;
            case 2u:
                return (int64_t)(int16_t)(uint16_t)bits;
            case 4u:
                return (int64_t)(int32_t)(uint32_t)bits;
            default:
                return (int64_t)bits;
        }
    }

    static bool is_unsafe_as_intrinsic(const struct zaclr_type_desc* type,
                                       const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "Unsafe")
            && text_equals(method->name.text, "As")
            && method->signature.parameter_count == 1u;
    }

    static bool is_unsafe_aspointer_intrinsic(const struct zaclr_type_desc* type,
                                              const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "Unsafe")
            && text_equals(method->name.text, "AsPointer")
            && method->signature.parameter_count == 1u;
    }

    static bool is_unsafe_add_intrinsic(const struct zaclr_type_desc* type,
                                        const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "Unsafe")
            && text_equals(method->name.text, "Add")
            && method->signature.parameter_count == 2u;
    }

    static bool is_vector_ishardwareaccelerated_intrinsic(const struct zaclr_type_desc* type,
                                                          const struct zaclr_method_desc* method)
    {
        if (type == NULL
            || method == NULL
            || type->type_namespace.text == NULL
            || type->type_name.text == NULL
            || method->name.text == NULL
            || !text_equals(method->name.text, "get_IsHardwareAccelerated")
            || method->signature.parameter_count != 0u)
        {
            return false;
        }

        if (text_equals(type->type_namespace.text, "System.Numerics")
            && text_equals(type->type_name.text, "Vector"))
        {
            return true;
        }

        if (!text_equals(type->type_namespace.text, "System.Runtime.Intrinsics"))
        {
            return false;
        }

        return text_equals(type->type_name.text, "Vector64")
            || text_equals(type->type_name.text, "Vector128")
            || text_equals(type->type_name.text, "Vector256")
            || text_equals(type->type_name.text, "Vector512")
            || text_equals(type->type_name.text, "ISimdVector`2");
    }

    static bool is_hardwareintrinsic_issupported_intrinsic(const struct zaclr_type_desc* type,
                                                          const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && method->name.text != NULL
            && text_equals(method->name.text, "get_IsSupported")
            && method->signature.parameter_count == 0u
            && (text_equals(type->type_namespace.text, "System.Runtime.Intrinsics.X86")
                || text_equals(type->type_namespace.text, "System.Runtime.Intrinsics.Arm")
                || text_equals(type->type_namespace.text, "System.Runtime.Intrinsics.Wasm"));
    }

    static bool is_inumberbase_isnegative_intrinsic(const struct zaclr_type_desc* type,
                                                    const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Numerics")
            && text_equals(type->type_name.text, "INumberBase`1")
            && text_equals(method->name.text, "IsNegative")
            && method->signature.parameter_count == 1u;
    }

    static bool try_get_string_runtime_intrinsic_kind(const struct zaclr_type_desc* type,
                                                      const struct zaclr_method_desc* method,
                                                      zaclr_string_intrinsic_kind* out_kind)
    {
        const bool has_this = method != NULL && ((method->signature.calling_convention & 0x20u) != 0u);

        if (out_kind != NULL)
        {
            *out_kind = zaclr_string_intrinsic_kind::none;
        }

        if (type == NULL
            || method == NULL
            || out_kind == NULL
            || type->type_namespace.text == NULL
            || type->type_name.text == NULL
            || method->name.text == NULL
            || !text_equals(type->type_namespace.text, "System")
            || !text_equals(type->type_name.text, "String"))
        {
            return false;
        }

        if (has_this && text_equals(method->name.text, "StartsWith") && method->signature.parameter_count >= 1u && method->signature.parameter_count <= 2u)
        {
            *out_kind = zaclr_string_intrinsic_kind::starts_with;
            return true;
        }

        if (has_this && text_equals(method->name.text, "EndsWith") && method->signature.parameter_count >= 1u && method->signature.parameter_count <= 2u)
        {
            *out_kind = zaclr_string_intrinsic_kind::ends_with;
            return true;
        }

        if (has_this && text_equals(method->name.text, "Contains") && method->signature.parameter_count >= 1u && method->signature.parameter_count <= 2u)
        {
            *out_kind = zaclr_string_intrinsic_kind::contains;
            return true;
        }

        if (!has_this && text_equals(method->name.text, "Compare") && method->signature.parameter_count >= 2u && method->signature.parameter_count <= 3u)
        {
            *out_kind = zaclr_string_intrinsic_kind::compare;
            return true;
        }

        if (has_this && text_equals(method->name.text, "ToUpper") && method->signature.parameter_count == 0u)
        {
            *out_kind = zaclr_string_intrinsic_kind::to_upper;
            return true;
        }

        if (has_this && text_equals(method->name.text, "ToLower") && method->signature.parameter_count == 0u)
        {
            *out_kind = zaclr_string_intrinsic_kind::to_lower;
            return true;
        }

        if (has_this && text_equals(method->name.text, "Trim") && method->signature.parameter_count == 0u)
        {
            *out_kind = zaclr_string_intrinsic_kind::trim;
            return true;
        }

        if (has_this && text_equals(method->name.text, "Replace") && method->signature.parameter_count == 2u)
        {
            *out_kind = zaclr_string_intrinsic_kind::replace;
            return true;
        }

        if (has_this && text_equals(method->name.text, "IndexOf") && method->signature.parameter_count == 1u)
        {
            *out_kind = zaclr_string_intrinsic_kind::index_of_char;
            return true;
        }

        return false;
    }

    static bool is_span_get_length_intrinsic(const struct zaclr_type_desc* type,
                                             const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System")
            && (text_equals(type->type_name.text, "Span`1")
                || text_equals(type->type_name.text, "ReadOnlySpan`1"))
            && text_equals(method->name.text, "get_Length")
            && method->signature.parameter_count == 0u
            && (method->signature.calling_convention & 0x20u) != 0u;
    }

    static bool is_memorymarshal_get_non_null_pinnable_reference_intrinsic(const struct zaclr_type_desc* type,
                                                                           const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.InteropServices")
            && text_equals(type->type_name.text, "MemoryMarshal")
            && text_equals(method->name.text, "GetNonNullPinnableReference")
            && method->signature.parameter_count == 1u;
    }

    static bool is_memorymarshal_get_reference_intrinsic(const struct zaclr_type_desc* type,
                                                         const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.InteropServices")
            && text_equals(type->type_name.text, "MemoryMarshal")
            && text_equals(method->name.text, "GetReference")
            && method->signature.parameter_count == 1u;
    }

    static bool is_bitconverter_bits_intrinsic(const struct zaclr_type_desc* type,
                                               const struct zaclr_method_desc* method)
    {
        if (type == NULL
            || method == NULL
            || type->type_namespace.text == NULL
            || type->type_name.text == NULL
            || method->name.text == NULL
            || !text_equals(type->type_namespace.text, "System")
            || !text_equals(type->type_name.text, "BitConverter")
            || method->signature.parameter_count != 1u)
        {
            return false;
        }

        return text_equals(method->name.text, "DoubleToInt64Bits")
            || text_equals(method->name.text, "DoubleToUInt64Bits")
            || text_equals(method->name.text, "Int32BitsToSingle")
            || text_equals(method->name.text, "Int64BitsToDouble")
            || text_equals(method->name.text, "SingleToInt32Bits")
            || text_equals(method->name.text, "SingleToUInt32Bits")
            || text_equals(method->name.text, "UInt32BitsToSingle")
            || text_equals(method->name.text, "UInt64BitsToDouble");
    }

    static bool is_binaryprimitives_reverseendianness_intrinsic(const struct zaclr_type_desc* type,
                                                                const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Buffers.Binary")
            && text_equals(type->type_name.text, "BinaryPrimitives")
            && text_equals(method->name.text, "ReverseEndianness")
            && method->signature.parameter_count == 1u;
    }

    static bool is_runtimehelpers_getmethodtable_intrinsic(const struct zaclr_type_desc* type,
                                                           const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "RuntimeHelpers")
            && text_equals(method->name.text, "GetMethodTable")
            && method->signature.parameter_count == 1u;
    }

    static bool is_runtimehelpers_objecthascomponentsize_intrinsic(const struct zaclr_type_desc* type,
                                                                   const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "RuntimeHelpers")
            && text_equals(method->name.text, "ObjectHasComponentSize")
            && method->signature.parameter_count == 1u;
    }

    /*
     * RuntimeHelpers.IsBitwiseEquatable<T>()
     * This is a JIT intrinsic.  Its IL body is `newobj NotSupportedException; throw`, which
     * is never reached by a native JIT compiler.  ZACLR must intercept it before the IL is
     * executed, otherwise the NotSupportedException allocation will OOM the managed heap
     * (the object accumulates with every call since there is no finalizer to reclaim it).
     * We always return false (0) so that callers (e.g. Array.LastIndexOf) take the
     * EqualityComparer<T> slow path, which is fully supported by ZACLR.
     */
    static bool is_runtimehelpers_isbitwiseequatable_intrinsic(const struct zaclr_type_desc* type,
                                                                const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "RuntimeHelpers")
            && text_equals(method->name.text, "IsBitwiseEquatable")
            && method->signature.parameter_count == 0u;
    }

    static bool is_runtimehelpers_isreferenceorcontainsreferences_intrinsic(const struct zaclr_type_desc* type,
                                                                            const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "RuntimeHelpers")
            && text_equals(method->name.text, "IsReferenceOrContainsReferences")
            && method->signature.parameter_count == 0u;
    }

    static bool is_runtimehelpers_isknownconstant_intrinsic(const struct zaclr_type_desc* type,
                                                            const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "RuntimeHelpers")
            && text_equals(method->name.text, "IsKnownConstant")
            && method->signature.parameter_count == 1u;
    }

    static bool is_gc_keepalive_intrinsic(const struct zaclr_type_desc* type,
                                          const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System")
            && text_equals(type->type_name.text, "GC")
            && text_equals(method->name.text, "KeepAlive")
            && method->signature.parameter_count == 1u;
    }

    static bool is_methodtable_hasfinalizer_intrinsic(const struct zaclr_type_desc* type,
                                                      const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "MethodTable")
            && text_equals(method->name.text, "get_HasFinalizer")
            && method->signature.parameter_count == 0u;
    }

    static bool is_methodtable_hascomponentsize_intrinsic(const struct zaclr_type_desc* type,
                                                          const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "MethodTable")
            && text_equals(method->name.text, "get_HasComponentSize")
            && method->signature.parameter_count == 0u;
    }

    static bool is_interlocked_method_named(const struct zaclr_type_desc* type,
                                            const struct zaclr_method_desc* method,
                                            const char* name)
    {
        return type != NULL
            && method != NULL
            && name != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && method->signature.generic_parameter_count == 0u
            && text_equals(type->type_namespace.text, "System.Threading")
            && text_equals(type->type_name.text, "Interlocked")
            && text_equals(method->name.text, name);
    }

    static bool is_interlocked_exchange_intrinsic(const struct zaclr_type_desc* type,
                                                  const struct zaclr_method_desc* method)
    {
        return is_interlocked_method_named(type, method, "Exchange")
            || is_interlocked_method_named(type, method, "Exchange32")
            || is_interlocked_method_named(type, method, "Exchange64")
            || is_interlocked_method_named(type, method, "ExchangeObject");
    }

    static bool is_interlocked_compareexchange_intrinsic(const struct zaclr_type_desc* type,
                                                         const struct zaclr_method_desc* method)
    {
        return is_interlocked_method_named(type, method, "CompareExchange")
            || is_interlocked_method_named(type, method, "CompareExchange32")
            || is_interlocked_method_named(type, method, "CompareExchange64")
            || is_interlocked_method_named(type, method, "CompareExchangeObject")
            || is_interlocked_method_named(type, method, "CompareExchange32Pointer");
    }

    static bool is_interlocked_exchangeadd_intrinsic(const struct zaclr_type_desc* type,
                                                     const struct zaclr_method_desc* method)
    {
        return is_interlocked_method_named(type, method, "ExchangeAdd")
            || is_interlocked_method_named(type, method, "ExchangeAdd32")
            || is_interlocked_method_named(type, method, "ExchangeAdd64");
    }

    static bool is_interlocked_add_intrinsic(const struct zaclr_type_desc* type,
                                             const struct zaclr_method_desc* method)
    {
        return is_interlocked_method_named(type, method, "Add");
    }

    static bool is_interlocked_and_intrinsic(const struct zaclr_type_desc* type,
                                             const struct zaclr_method_desc* method)
    {
        return is_interlocked_method_named(type, method, "And");
    }

    static bool is_interlocked_or_intrinsic(const struct zaclr_type_desc* type,
                                            const struct zaclr_method_desc* method)
    {
        return is_interlocked_method_named(type, method, "Or");
    }

    static bool is_interlocked_increment_intrinsic(const struct zaclr_type_desc* type,
                                                   const struct zaclr_method_desc* method)
    {
        return is_interlocked_method_named(type, method, "Increment");
    }

    static bool is_interlocked_decrement_intrinsic(const struct zaclr_type_desc* type,
                                                   const struct zaclr_method_desc* method)
    {
        return is_interlocked_method_named(type, method, "Decrement");
    }

    static bool is_interlocked_read_intrinsic(const struct zaclr_type_desc* type,
                                              const struct zaclr_method_desc* method)
    {
        return is_interlocked_method_named(type, method, "Read");
    }

    static bool is_interlocked_memorybarrier_intrinsic(const struct zaclr_type_desc* type,
                                                       const struct zaclr_method_desc* method)
    {
        return is_interlocked_method_named(type, method, "MemoryBarrier")
            && method->signature.parameter_count == 0u;
    }

    static bool is_volatile_method_named(const struct zaclr_type_desc* type,
                                         const struct zaclr_method_desc* method,
                                         const char* name)
    {
        return type != NULL
            && method != NULL
            && name != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Threading")
            && text_equals(type->type_name.text, "Volatile")
            && text_equals(method->name.text, name);
    }

    static bool is_volatile_read_intrinsic(const struct zaclr_type_desc* type,
                                           const struct zaclr_method_desc* method)
    {
        return is_volatile_method_named(type, method, "Read")
            && method->signature.parameter_count == 1u;
    }

    static bool is_volatile_write_intrinsic(const struct zaclr_type_desc* type,
                                            const struct zaclr_method_desc* method)
    {
        return is_volatile_method_named(type, method, "Write")
            && method->signature.parameter_count == 2u;
    }

    static bool is_volatile_barrier_intrinsic(const struct zaclr_type_desc* type,
                                              const struct zaclr_method_desc* method)
    {
        return (is_volatile_method_named(type, method, "ReadBarrier")
                || is_volatile_method_named(type, method, "WriteBarrier"))
            && method->signature.parameter_count == 0u;
    }

    static bool is_gchandle_gethandlevalue_intrinsic(const struct zaclr_type_desc* type,
                                                     const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.InteropServices")
            && text_equals(type->type_name.text, "GCHandle")
            && text_equals(method->name.text, "GetHandleValue")
            && method->signature.parameter_count == 1u;
    }

    static bool is_objecthandleonstack_create_intrinsic(const struct zaclr_type_desc* type,
                                                        const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "ObjectHandleOnStack")
            && text_equals(method->name.text, "Create")
            && method->signature.parameter_count == 1u;
    }

    static bool is_monitor_enter_intrinsic(const struct zaclr_type_desc* type,
                                           const struct zaclr_method_desc* method)
    {
        return type != NULL
            && method != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Threading")
            && text_equals(type->type_name.text, "Monitor")
            && text_equals(method->name.text, "Enter")
            && method->signature.parameter_count == 2u;
    }

    static bool is_unsafe_named_intrinsic(const struct zaclr_type_desc* type,
                                          const struct zaclr_method_desc* method,
                                          const char* name)
    {
        return type != NULL
            && method != NULL
            && name != NULL
            && type->type_namespace.text != NULL
            && type->type_name.text != NULL
            && method->name.text != NULL
            && text_equals(type->type_namespace.text, "System.Runtime.CompilerServices")
            && text_equals(type->type_name.text, "Unsafe")
            && text_equals(method->name.text, name);
    }
}

static uint32_t unsafe_generic_argument_width(const struct zaclr_generic_argument* argument)
{
    uint32_t width;

    if (argument == NULL)
    {
        return 0u;
    }

    width = numeric_width_from_element_type(argument->element_type);
    if (width != 0u)
    {
        return width;
    }

    return zaclr_field_layout_size_from_element_type(argument->element_type);
}

static struct zaclr_result invoke_unsafe_as_intrinsic(struct zaclr_frame* frame,
                                                      const struct zaclr_method_desc* method)
{
    struct zaclr_stack_value value = {};
    const struct zaclr_generic_argument* destination_argument;
    uint32_t destination_width = 0u;
    struct zaclr_result result;

    ZACLR_TRACE_VALUE(frame != NULL ? frame->runtime : NULL,
                      ZACLR_TRACE_CATEGORY_EXEC,
                      ZACLR_TRACE_EVENT_CALL_TARGET,
                      "UnsafeAsIntrinsic",
                      1u);

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        console_write("[ZACLR][intrinsic] exchange pop value failed status=");
        console_write_dec((uint64_t)result.status);
        console_write("\n");
        return result;
    }

    (void)method;

    if (frame->generic_context.method_arg_count >= 2u
        && (value.kind == ZACLR_STACK_VALUE_BYREF || value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS))
    {
        destination_argument = zaclr_generic_context_get_method_argument(&frame->generic_context, 1u);
        destination_width = unsafe_generic_argument_width(destination_argument);
        if (destination_width != 0u)
        {
            value.payload_size = destination_width;
        }
    }

    return zaclr_eval_stack_push(&frame->eval_stack, &value);
}

static struct zaclr_result invoke_unsafe_aspointer_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_result result;
    uintptr_t pointer_value = 0u;

    ZACLR_TRACE_VALUE(frame != NULL ? frame->runtime : NULL,
                      ZACLR_TRACE_CATEGORY_EXEC,
                      ZACLR_TRACE_EVENT_CALL_TARGET,
                      "UnsafeAsPointerIntrinsic",
                      1u);

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (value.kind == ZACLR_STACK_VALUE_BYREF)
    {
        pointer_value = (uintptr_t)value.data.raw;
    }
    else if (value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        struct zaclr_stack_value* target = resolve_local_address_target(frame, &value);
        if (target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        pointer_value = (uintptr_t)target;
    }
    else if (value.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        pointer_value = (uintptr_t)value.data.object_reference;
    }
    else if (value.kind == ZACLR_STACK_VALUE_I8)
    {
        pointer_value = (uintptr_t)value.data.i8;
    }
    else if (value.kind == ZACLR_STACK_VALUE_I4)
    {
        pointer_value = (uintptr_t)(uint32_t)value.data.i4;
    }
    else
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    return push_i8(frame, (int64_t)pointer_value);
}


static struct zaclr_result invoke_unsafe_add_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value index_value = {};
    struct zaclr_stack_value byref_value = {};
    const struct zaclr_generic_argument* element_argument;
    struct zaclr_result result;
    uint32_t element_width = 0u;
    int64_t index = 0;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &index_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &byref_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (byref_value.kind != ZACLR_STACK_VALUE_BYREF && byref_value.kind != ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (index_value.kind == ZACLR_STACK_VALUE_I4)
    {
        index = (int64_t)index_value.data.i4;
    }
    else if (index_value.kind == ZACLR_STACK_VALUE_I8)
    {
        index = index_value.data.i8;
    }
    else
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    element_argument = zaclr_generic_context_get_method_argument(&frame->generic_context, 0u);
    element_width = unsafe_generic_argument_width(element_argument);
    if (element_width != 0u)
    {
        byref_value.payload_size = element_width;
    }

    if (byref_value.payload_size == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    byref_value.data.raw = (uintptr_t)((uint8_t*)(uintptr_t)byref_value.data.raw + (index * (int64_t)byref_value.payload_size));
    return zaclr_eval_stack_push(&frame->eval_stack, &byref_value);
}

static struct zaclr_result invoke_unsafe_byte_offset_intrinsic(struct zaclr_frame* frame,
                                                               int64_t direction)
{
    struct zaclr_stack_value offset_value = {};
    struct zaclr_stack_value byref_value = {};
    uintptr_t address = 0u;
    uint32_t payload_size = 0u;
    uint32_t type_token_raw = 0u;
    int64_t offset = 0;
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &offset_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &byref_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (offset_value.kind == ZACLR_STACK_VALUE_I8)
    {
        offset = offset_value.data.i8;
    }
    else if (offset_value.kind == ZACLR_STACK_VALUE_I4)
    {
        offset = (int64_t)offset_value.data.i4;
    }
    else
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = raw_address_from_stack_value(frame, &byref_value, &address, &payload_size, &type_token_raw);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_stack_value_set_byref(&byref_value,
                                       (void*)(uintptr_t)(address + (uintptr_t)(offset * direction)),
                                       payload_size,
                                       type_token_raw,
                                       ZACLR_STACK_VALUE_FLAG_NONE).status == ZACLR_STATUS_OK
        ? zaclr_eval_stack_push(&frame->eval_stack, &byref_value)
        : zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
}

static struct zaclr_result invoke_unsafe_are_same_or_offset_intrinsic(struct zaclr_frame* frame,
                                                                      bool return_offset)
{
    struct zaclr_stack_value right = {};
    struct zaclr_stack_value left = {};
    uintptr_t right_address = 0u;
    uintptr_t left_address = 0u;
    uint32_t ignored_size = 0u;
    uint32_t ignored_token = 0u;
    struct zaclr_result result;

    if (frame == NULL)
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

    result = raw_address_from_stack_value(frame, &left, &left_address, &ignored_size, &ignored_token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = raw_address_from_stack_value(frame, &right, &right_address, &ignored_size, &ignored_token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return return_offset
        ? push_i8(frame, (int64_t)(right_address - left_address))
        : push_i4(frame, left_address == right_address ? 1 : 0);
}

static struct zaclr_result invoke_unsafe_nullref_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_stack_value_set_byref(&value, NULL, 0u, 0u, ZACLR_STACK_VALUE_FLAG_NONE);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_eval_stack_push(&frame->eval_stack, &value);
}

static struct zaclr_result invoke_unsafe_isnullref_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (value.kind == ZACLR_STACK_VALUE_BYREF && (value.flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) == 0u)
    {
        return push_i4(frame, value.data.raw == 0u ? 1 : 0);
    }

    return push_i4(frame, 0);
}

static struct zaclr_result invoke_unsafe_asref_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    uintptr_t address = 0u;
    uint32_t payload_size = 0u;
    uint32_t type_token_raw = 0u;
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (value.kind == ZACLR_STACK_VALUE_BYREF || value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        return zaclr_eval_stack_push(&frame->eval_stack, &value);
    }

    result = raw_address_from_stack_value(frame, &value, &address, &payload_size, &type_token_raw);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_stack_value_set_byref(&value,
                                         (void*)address,
                                         payload_size,
                                         type_token_raw,
                                         ZACLR_STACK_VALUE_FLAG_NONE);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_eval_stack_push(&frame->eval_stack, &value);
}

static struct zaclr_result invoke_unsafe_skipinit_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value ignored = {};

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    return zaclr_eval_stack_pop(&frame->eval_stack, &ignored);
}

static struct zaclr_result push_unaligned_read_value(struct zaclr_frame* frame,
                                                     uint8_t element_type,
                                                     uint32_t type_token_raw,
                                                     const void* source,
                                                     uint32_t width)
{
    uint64_t bits = 0u;
    struct zaclr_stack_value value = {};
    struct zaclr_result result;

    if (frame == NULL || source == NULL || width == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (width > sizeof(bits))
    {
        result = zaclr_stack_value_set_valuetype(&value, type_token_raw, source, width);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return zaclr_eval_stack_push(&frame->eval_stack, &value);
    }

    kernel_memcpy(&bits, source, width);
    switch (element_type)
    {
        case ZACLR_ELEMENT_TYPE_BOOLEAN:
        case ZACLR_ELEMENT_TYPE_U1:
            return push_i4(frame, (int32_t)(uint8_t)bits);

        case ZACLR_ELEMENT_TYPE_I1:
            return push_i4(frame, (int32_t)(int8_t)(uint8_t)bits);

        case ZACLR_ELEMENT_TYPE_CHAR:
        case ZACLR_ELEMENT_TYPE_U2:
            return push_i4(frame, (int32_t)(uint16_t)bits);

        case ZACLR_ELEMENT_TYPE_I2:
            return push_i4(frame, (int32_t)(int16_t)(uint16_t)bits);

        case ZACLR_ELEMENT_TYPE_I4:
        case ZACLR_ELEMENT_TYPE_U4:
            return push_i4(frame, (int32_t)(uint32_t)bits);

        case ZACLR_ELEMENT_TYPE_R4:
            return push_r4(frame, (uint32_t)bits);

        case ZACLR_ELEMENT_TYPE_I:
        case ZACLR_ELEMENT_TYPE_U:
        case ZACLR_ELEMENT_TYPE_I8:
        case ZACLR_ELEMENT_TYPE_U8:
            return push_i8(frame, (int64_t)bits);

        case ZACLR_ELEMENT_TYPE_R8:
            return push_r8(frame, bits);

        case ZACLR_ELEMENT_TYPE_CLASS:
        case ZACLR_ELEMENT_TYPE_OBJECT:
        case ZACLR_ELEMENT_TYPE_STRING:
        case ZACLR_ELEMENT_TYPE_SZARRAY:
            return push_object_reference(frame, (struct zaclr_object_desc*)(uintptr_t)bits);

        default:
            result = zaclr_stack_value_set_valuetype(&value, type_token_raw, &bits, width);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            return zaclr_eval_stack_push(&frame->eval_stack, &value);
    }
}

static struct zaclr_result invoke_unsafe_read_unaligned_intrinsic(struct zaclr_frame* frame,
                                                                 const struct zaclr_method_desc* method)
{
    struct zaclr_stack_value source_value = {};
    const struct zaclr_generic_argument* generic_argument;
    uintptr_t address = 0u;
    uint32_t payload_size = 0u;
    uint32_t source_type_token_raw = 0u;
    uint8_t element_type = 0u;
    uint32_t type_token_raw = 0u;
    uint32_t width = 0u;
    struct zaclr_result result;

    if (frame == NULL || method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &source_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    generic_argument = zaclr_generic_context_get_method_argument(&frame->generic_context, 0u);
    if (generic_argument != NULL)
    {
        element_type = generic_argument->element_type;
        type_token_raw = generic_argument->token.raw;
    }

    if (element_type == 0u)
    {
        element_type = method->signature.return_type.element_type;
        type_token_raw = method->signature.return_type.type_token.raw;
    }

    width = numeric_width_from_element_type(element_type);
    if (width == 0u)
    {
        width = zaclr_field_layout_size_from_element_type(element_type);
    }

    result = raw_address_from_stack_value(frame, &source_value, &address, &payload_size, &source_type_token_raw);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (address == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (width == 0u)
    {
        width = payload_size;
    }

    if (type_token_raw == 0u)
    {
        type_token_raw = source_type_token_raw;
    }

    if (width == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    return push_unaligned_read_value(frame,
                                     element_type,
                                     type_token_raw,
                                     (const void*)(uintptr_t)address,
                                     width);
}

static struct zaclr_result write_unaligned_stack_value(void* destination,
                                                       uint32_t width,
                                                       const struct zaclr_stack_value* value)
{
    const void* payload;
    uint64_t bits;
    struct zaclr_object_desc* object_reference;

    if (destination == NULL || width == 0u || value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (value->kind == ZACLR_STACK_VALUE_VALUETYPE)
    {
        payload = zaclr_stack_value_payload_const(value);
        if (payload == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        kernel_memcpy(destination, payload, value->payload_size < width ? value->payload_size : width);
        return zaclr_result_ok();
    }

    if (value->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        object_reference = value->data.object_reference;
        kernel_memcpy(destination, &object_reference, width < sizeof(object_reference) ? width : sizeof(object_reference));
        return zaclr_result_ok();
    }

    bits = numeric_stack_value_bits(value, width);
    kernel_memcpy(destination, &bits, width < sizeof(bits) ? width : sizeof(bits));
    return zaclr_result_ok();
}

static struct zaclr_result invoke_unsafe_write_unaligned_intrinsic(struct zaclr_frame* frame,
                                                                  const struct zaclr_method_desc* method)
{
    struct zaclr_stack_value value = {};
    struct zaclr_stack_value destination_value = {};
    const struct zaclr_generic_argument* generic_argument;
    uintptr_t address = 0u;
    uint32_t payload_size = 0u;
    uint32_t destination_type_token_raw = 0u;
    uint8_t element_type = 0u;
    uint32_t width = 0u;
    struct zaclr_result result;

    if (frame == NULL || method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &destination_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    generic_argument = zaclr_generic_context_get_method_argument(&frame->generic_context, 0u);
    if (generic_argument != NULL)
    {
        element_type = generic_argument->element_type;
    }

    if (element_type == 0u && method->signature.parameter_count >= 2u)
    {
        struct zaclr_signature_type parameter_type = {};
        result = zaclr_signature_read_method_parameter(&method->signature, 1u, &parameter_type);
        if (result.status == ZACLR_STATUS_OK)
        {
            element_type = parameter_type.element_type;
        }
    }

    width = numeric_width_from_element_type(element_type);
    if (width == 0u)
    {
        width = zaclr_field_layout_size_from_element_type(element_type);
    }

    result = raw_address_from_stack_value(frame, &destination_value, &address, &payload_size, &destination_type_token_raw);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (address == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (width == 0u)
    {
        width = numeric_width_from_stack_value(&value);
    }

    if (width == 0u)
    {
        width = payload_size;
    }

    if (width == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    (void)destination_type_token_raw;
    return write_unaligned_stack_value((void*)(uintptr_t)address, width, &value);
}

static struct zaclr_result invoke_vector_ishardwareaccelerated_intrinsic(struct zaclr_frame* frame)
{
    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    return push_i4(frame, 0);
}

static struct zaclr_result invoke_inumberbase_isnegative_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    const struct zaclr_generic_argument* generic_argument;
    uint32_t width = 0u;
    uint64_t bits;
    bool is_unsigned = false;
    bool is_negative = false;
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    generic_argument = zaclr_generic_context_get_method_argument(&frame->generic_context, 0u);
    if (generic_argument != NULL)
    {
        width = numeric_width_from_element_type(generic_argument->element_type);
        is_unsigned = numeric_element_type_is_unsigned(generic_argument->element_type);
    }

    if (width == 0u)
    {
        width = numeric_width_from_stack_value(&value);
    }

    if (width == 0u || width > 8u)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    bits = numeric_stack_value_bits(&value, width);
    if (!is_unsigned)
    {
        if (value.kind == ZACLR_STACK_VALUE_R4)
        {
            is_negative = (bits & 0x80000000ull) != 0u;
        }
        else if (value.kind == ZACLR_STACK_VALUE_R8)
        {
            is_negative = (bits & 0x8000000000000000ull) != 0u;
        }
        else
        {
            is_negative = numeric_sign_extend(bits, width) < 0;
        }
    }

    return push_i4(frame, is_negative ? 1 : 0);
}

static const struct zaclr_string_desc* string_from_stack_value(struct zaclr_frame* frame,
                                                               const struct zaclr_stack_value* value)
{
    zaclr_object_handle handle;

    if (frame == NULL
        || frame->runtime == NULL
        || value == NULL
        || value->kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE
        || value->data.object_reference == NULL)
    {
        return NULL;
    }

    handle = zaclr_heap_get_object_handle(&frame->runtime->heap, value->data.object_reference);
    return handle == 0u ? NULL : zaclr_string_from_handle_const(&frame->runtime->heap, handle);
}

static bool string_region_equals(const struct zaclr_string_desc* text,
                                 uint32_t text_offset,
                                 const struct zaclr_string_desc* value)
{
    uint32_t index;

    if (text == NULL || value == NULL || text_offset + zaclr_string_length(value) > zaclr_string_length(text))
    {
        return false;
    }

    for (index = 0u; index < zaclr_string_length(value); ++index)
    {
        if (zaclr_string_char_at(text, text_offset + index) != zaclr_string_char_at(value, index))
        {
            return false;
        }
    }

    return true;
}

static int32_t string_compare_ordinal(const struct zaclr_string_desc* left,
                                      const struct zaclr_string_desc* right)
{
    uint32_t index;
    uint32_t left_length;
    uint32_t right_length;
    uint32_t min_length;

    if (left == right)
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

static uint16_t string_uppercase_ascii(uint16_t value)
{
    return value >= (uint16_t)'a' && value <= (uint16_t)'z' ? (uint16_t)(value - 32u) : value;
}

static uint16_t string_lowercase_ascii(uint16_t value)
{
    return value >= (uint16_t)'A' && value <= (uint16_t)'Z' ? (uint16_t)(value + 32u) : value;
}

static struct zaclr_result push_string_copy(struct zaclr_frame* frame,
                                            const uint16_t* text,
                                            uint32_t length)
{
    struct zaclr_string_desc* string_object = NULL;
    struct zaclr_result result;

    if (frame == NULL || frame->runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_string_allocate_utf16(&frame->runtime->heap, text, length, &string_object);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return push_object_reference(frame, &string_object->object);
}

static struct zaclr_result invoke_string_search_intrinsic(struct zaclr_frame* frame,
                                                          zaclr_string_intrinsic_kind kind)
{
    struct zaclr_stack_value value_arg = {};
    struct zaclr_stack_value ignored_comparison = {};
    struct zaclr_stack_value this_value = {};
    const struct zaclr_string_desc* text;
    const struct zaclr_string_desc* value;
    bool matches = false;
    struct zaclr_result result;

    if (frame == NULL || frame->method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (frame->method->signature.parameter_count == 2u)
    {
        result = zaclr_eval_stack_pop(&frame->eval_stack, &ignored_comparison);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value_arg);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &this_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    text = string_from_stack_value(frame, &this_value);
    value = string_from_stack_value(frame, &value_arg);
    if (text == NULL || value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (kind == zaclr_string_intrinsic_kind::starts_with)
    {
        matches = string_region_equals(text, 0u, value);
    }
    else if (kind == zaclr_string_intrinsic_kind::ends_with)
    {
        const uint32_t text_length = zaclr_string_length(text);
        const uint32_t value_length = zaclr_string_length(value);
        matches = value_length <= text_length && string_region_equals(text, text_length - value_length, value);
    }
    else if (kind == zaclr_string_intrinsic_kind::contains)
    {
        uint32_t offset;
        matches = zaclr_string_length(value) == 0u;
        for (offset = 0u; !matches && offset + zaclr_string_length(value) <= zaclr_string_length(text); ++offset)
        {
            matches = string_region_equals(text, offset, value);
        }
    }

    zaclr_stack_value_reset(&this_value);
    zaclr_stack_value_reset(&value_arg);
    zaclr_stack_value_reset(&ignored_comparison);
    return push_i4(frame, matches ? 1 : 0);
}

static struct zaclr_result invoke_string_compare_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value right_value = {};
    struct zaclr_stack_value left_value = {};
    struct zaclr_stack_value ignored_comparison = {};
    struct zaclr_result result;

    if (frame == NULL || frame->method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (frame->method->signature.parameter_count == 3u)
    {
        result = zaclr_eval_stack_pop(&frame->eval_stack, &ignored_comparison);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &right_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &left_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return push_i4(frame,
                   string_compare_ordinal(string_from_stack_value(frame, &left_value),
                                          string_from_stack_value(frame, &right_value)));
}

static struct zaclr_result invoke_string_transform_intrinsic(struct zaclr_frame* frame,
                                                             zaclr_string_intrinsic_kind kind)
{
    struct zaclr_stack_value this_value = {};
    const struct zaclr_string_desc* text;
    uint32_t start = 0u;
    uint32_t end;
    uint32_t index;
    uint16_t* buffer;
    struct zaclr_result result;

    result = zaclr_eval_stack_pop(&frame->eval_stack, &this_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    text = string_from_stack_value(frame, &this_value);
    if (text == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    end = zaclr_string_length(text);
    if (kind == zaclr_string_intrinsic_kind::trim)
    {
        while (start < end && zaclr_string_char_at(text, start) == (uint16_t)' ')
        {
            ++start;
        }
        while (end > start && zaclr_string_char_at(text, end - 1u) == (uint16_t)' ')
        {
            --end;
        }
    }

    buffer = end != start ? (uint16_t*)kernel_alloc(sizeof(uint16_t) * (end - start)) : NULL;
    if (end != start && buffer == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_EXEC);
    }

    for (index = 0u; index < end - start; ++index)
    {
        uint16_t character = zaclr_string_char_at(text, start + index);
        if (kind == zaclr_string_intrinsic_kind::to_upper)
        {
            character = string_uppercase_ascii(character);
        }
        else if (kind == zaclr_string_intrinsic_kind::to_lower)
        {
            character = string_lowercase_ascii(character);
        }
        buffer[index] = character;
    }

    result = push_string_copy(frame, buffer, end - start);
    if (buffer != NULL)
    {
        kernel_free(buffer);
    }
    return result;
}

static struct zaclr_result invoke_string_replace_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value new_value_arg = {};
    struct zaclr_stack_value old_value_arg = {};
    struct zaclr_stack_value this_value = {};
    const struct zaclr_string_desc* text;
    const struct zaclr_string_desc* old_value;
    const struct zaclr_string_desc* new_value;
    uint32_t capacity;
    uint32_t out_length;
    uint32_t index;
    uint16_t* buffer;
    struct zaclr_result result;

    result = zaclr_eval_stack_pop(&frame->eval_stack, &new_value_arg);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &old_value_arg);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &this_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    text = string_from_stack_value(frame, &this_value);
    old_value = string_from_stack_value(frame, &old_value_arg);
    new_value = string_from_stack_value(frame, &new_value_arg);
    if (text == NULL || old_value == NULL || new_value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (zaclr_string_length(old_value) == 0u)
    {
        return push_string_copy(frame, zaclr_string_code_units(text), zaclr_string_length(text));
    }

    capacity = (zaclr_string_length(text) + 1u) * (zaclr_string_length(new_value) + 1u);
    buffer = capacity != 0u ? (uint16_t*)kernel_alloc(sizeof(uint16_t) * capacity) : NULL;
    if (capacity != 0u && buffer == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_EXEC);
    }

    out_length = 0u;
    index = 0u;
    while (index < zaclr_string_length(text))
    {
        uint32_t match_index;
        bool matched = false;
        if (index + zaclr_string_length(old_value) <= zaclr_string_length(text))
        {
            matched = true;
            for (match_index = 0u; match_index < zaclr_string_length(old_value); ++match_index)
            {
                if (zaclr_string_char_at(text, index + match_index) != zaclr_string_char_at(old_value, match_index))
                {
                    matched = false;
                    break;
                }
            }
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
            buffer[out_length++] = zaclr_string_char_at(text, index);
            ++index;
        }
    }

    result = push_string_copy(frame, buffer, out_length);
    if (buffer != NULL)
    {
        kernel_free(buffer);
    }

    zaclr_stack_value_reset(&this_value);
    zaclr_stack_value_reset(&old_value_arg);
    zaclr_stack_value_reset(&new_value_arg);
    return result;
}

static struct zaclr_result invoke_string_indexof_char_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value needle_value = {};
    struct zaclr_stack_value this_value = {};
    const struct zaclr_string_desc* text;
    uint32_t index;
    int32_t needle;
    struct zaclr_result result;

    result = zaclr_eval_stack_pop(&frame->eval_stack, &needle_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &this_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (needle_value.kind != ZACLR_STACK_VALUE_I4)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    text = string_from_stack_value(frame, &this_value);
    if (text == NULL)
    {
        return push_i4(frame, -1);
    }

    needle = needle_value.data.i4;
    for (index = 0u; index < zaclr_string_length(text); ++index)
    {
        if ((int32_t)zaclr_string_char_at(text, index) == needle)
        {
            return push_i4(frame, (int32_t)index);
        }
    }

    return push_i4(frame, -1);
}

static struct zaclr_result invoke_string_intrinsic(struct zaclr_frame* frame,
                                                   zaclr_string_intrinsic_kind kind)
{
    if (kind == zaclr_string_intrinsic_kind::starts_with
        || kind == zaclr_string_intrinsic_kind::ends_with
        || kind == zaclr_string_intrinsic_kind::contains)
    {
        return invoke_string_search_intrinsic(frame, kind);
    }

    if (kind == zaclr_string_intrinsic_kind::compare)
    {
        return invoke_string_compare_intrinsic(frame);
    }

    if (kind == zaclr_string_intrinsic_kind::to_upper
        || kind == zaclr_string_intrinsic_kind::to_lower
        || kind == zaclr_string_intrinsic_kind::trim)
    {
        return invoke_string_transform_intrinsic(frame, kind);
    }

    if (kind == zaclr_string_intrinsic_kind::replace)
    {
        return invoke_string_replace_intrinsic(frame);
    }

    if (kind == zaclr_string_intrinsic_kind::index_of_char)
    {
        return invoke_string_indexof_char_intrinsic(frame);
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
}

static struct zaclr_result try_read_span_payload(struct zaclr_frame* frame,
                                                const struct zaclr_stack_value* value,
                                                const struct zaclr_field_layout* reference_layout,
                                                const struct zaclr_field_layout* length_layout,
                                                const uint8_t** out_payload,
                                                uint32_t* out_payload_size,
                                                uint32_t* out_payload_type_token)
{
    const uint8_t* payload;
    uint32_t payload_size = 0u;
    uint32_t payload_type_token = 0u;

    if (out_payload == NULL || out_payload_size == NULL || out_payload_type_token == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    *out_payload = NULL;
    *out_payload_size = 0u;
    *out_payload_type_token = 0u;

    if (frame == NULL || value == NULL || length_layout == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    payload = read_payload_from_stack_value(frame, value, &payload_size, &payload_type_token);
    if (payload != NULL
        && payload_size >= (length_layout->byte_offset + length_layout->field_size)
        && (reference_layout == NULL || payload_size >= (reference_layout->byte_offset + reference_layout->field_size)))
    {
        *out_payload = payload;
        *out_payload_size = payload_size;
        *out_payload_type_token = payload_type_token;
        return zaclr_result_ok();
    }

    if (value->kind == ZACLR_STACK_VALUE_VALUETYPE)
    {
        payload = (const uint8_t*)zaclr_stack_value_payload_const(value);
        payload_size = value->payload_size;
        payload_type_token = value->type_token_raw;
        if (payload != NULL
            && payload_size >= (length_layout->byte_offset + length_layout->field_size)
            && (reference_layout == NULL || payload_size >= (reference_layout->byte_offset + reference_layout->field_size)))
        {
            *out_payload = payload;
            *out_payload_size = payload_size;
            *out_payload_type_token = payload_type_token;
            return zaclr_result_ok();
        }
    }

    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
}

static bool try_get_span_length_from_split_stack_value(const struct zaclr_stack_value* value,
                                                       int32_t* out_length)
{
    const struct zaclr_stack_value* target;

    if (value == NULL || out_length == NULL)
    {
        return false;
    }

    if (value->kind == ZACLR_STACK_VALUE_I4)
    {
        *out_length = value->data.i4;
        return true;
    }

    if (value->kind == ZACLR_STACK_VALUE_I8)
    {
        *out_length = (int32_t)value->data.i8;
        return true;
    }

    if (value->kind == ZACLR_STACK_VALUE_BYREF
        && (value->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) != 0u
        && value->data.raw != 0u)
    {
        target = (const struct zaclr_stack_value*)(uintptr_t)value->data.raw;
        if (target->kind == ZACLR_STACK_VALUE_I4)
        {
            *out_length = target->data.i4;
            return true;
        }

        if (target->kind == ZACLR_STACK_VALUE_I8)
        {
            *out_length = (int32_t)target->data.i8;
            return true;
        }
    }

    return false;
}

static bool try_get_span_reference_address_from_split_stack_value(struct zaclr_frame* frame,
                                                                  const struct zaclr_stack_value* value,
                                                                  uintptr_t* out_address)
{
    uintptr_t address = 0u;
    uint32_t ignored_size = 0u;
    uint32_t ignored_token = 0u;
    struct zaclr_result result;

    if (out_address == NULL)
    {
        return false;
    }

    *out_address = 0u;
    if (value == NULL)
    {
        return false;
    }

    if (value->kind == ZACLR_STACK_VALUE_I8)
    {
        *out_address = (uintptr_t)value->data.i8;
        return true;
    }

    if (value->kind == ZACLR_STACK_VALUE_I4)
    {
        *out_address = (uintptr_t)(uint32_t)value->data.i4;
        return true;
    }

    result = raw_address_from_stack_value(frame, value, &address, &ignored_size, &ignored_token);
    if (result.status != ZACLR_STATUS_OK)
    {
        return false;
    }

    *out_address = address;
    return true;
}

static struct zaclr_result try_push_split_span_reference(struct zaclr_frame* frame,
                                                         const struct zaclr_stack_value* length_value,
                                                         uint32_t payload_type_token,
                                                         uint32_t payload_size,
                                                         bool require_non_empty)
{
    struct zaclr_stack_value reference_value = {};
    const struct zaclr_stack_value* reference_source = NULL;
    struct zaclr_stack_value byref_value = {};
    int32_t length = 0;
    uintptr_t referenced_address = 0u;
    bool pop_reference_from_eval_stack = false;
    struct zaclr_result result;

    if (frame == NULL || length_value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (!try_get_span_length_from_split_stack_value(length_value, &length))
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (frame->eval_stack.depth != 0u)
    {
        result = zaclr_eval_stack_peek(&frame->eval_stack, &reference_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        reference_source = &reference_value;
        pop_reference_from_eval_stack = true;
    }
    else if (frame->argument_count != 0u && frame->arguments != NULL)
    {
        reference_source = &frame->arguments[0];
    }
    else
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (require_non_empty && length == 0)
    {
        referenced_address = 1u;
    }
    else if (!try_get_span_reference_address_from_split_stack_value(frame, reference_source, &referenced_address))
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_stack_value_set_byref(&byref_value,
                                         (void*)referenced_address,
                                         payload_size,
                                         payload_type_token,
                                         ZACLR_STACK_VALUE_FLAG_NONE);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (pop_reference_from_eval_stack)
    {
        result = zaclr_eval_stack_pop(&frame->eval_stack, &reference_value);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
    }

    return zaclr_eval_stack_push(&frame->eval_stack, &byref_value);
}

static struct zaclr_result invoke_span_get_length_intrinsic(struct zaclr_frame* frame,
                                                            const struct zaclr_loaded_assembly* assembly,
                                                            const struct zaclr_type_desc* type)
{
    struct zaclr_stack_value receiver = {};
    const struct zaclr_field_layout* length_layout;
    const uint8_t* payload;
    uint32_t payload_size = 0u;
    uint32_t ignored_token = 0u;
    int32_t length = 0;
    struct zaclr_result result;

    if (frame == NULL || assembly == NULL || type == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &receiver);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    length_layout = find_instance_field_layout_by_name(frame->runtime,
                                                       assembly,
                                                       type,
                                                       "_length");
    if (length_layout == NULL || length_layout->field_size == 0u || length_layout->field_size > sizeof(length))
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = try_read_span_payload(frame,
                                   &receiver,
                                   NULL,
                                   length_layout,
                                   &payload,
                                   &payload_size,
                                   &ignored_token);
    if (result.status == ZACLR_STATUS_OK)
    {
        kernel_memcpy(&length, payload + length_layout->byte_offset, length_layout->field_size);
        return push_i4(frame, length);
    }

    if (try_get_span_length_from_split_stack_value(&receiver, &length))
    {
        return push_i4(frame, length);
    }

    console_write("[ZACLR][span-len] failed receiver_kind=");
    console_write_dec((uint64_t)receiver.kind);
    console_write(" receiver_flags=");
    console_write_hex64((uint64_t)receiver.flags);
    console_write(" receiver_payload=");
    console_write_dec((uint64_t)receiver.payload_size);
    console_write(" receiver_type=");
    console_write_hex64((uint64_t)receiver.type_token_raw);
    console_write(" receiver_raw=");
    console_write_hex64((uint64_t)receiver.data.raw);
    if (receiver.kind == ZACLR_STACK_VALUE_BYREF
        && (receiver.flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) != 0u
        && receiver.data.raw != 0u)
    {
        const struct zaclr_stack_value* receiver_target = (const struct zaclr_stack_value*)(uintptr_t)receiver.data.raw;
        console_write(" target_kind=");
        console_write_dec((uint64_t)receiver_target->kind);
        console_write(" target_flags=");
        console_write_hex64((uint64_t)receiver_target->flags);
        console_write(" target_payload=");
        console_write_dec((uint64_t)receiver_target->payload_size);
        console_write(" target_type=");
        console_write_hex64((uint64_t)receiver_target->type_token_raw);
        console_write(" target_raw=");
        console_write_hex64((uint64_t)receiver_target->data.raw);
    }
    if (frame->argument_count != 0u && frame->arguments != NULL)
    {
        console_write(" arg0_kind=");
        console_write_dec((uint64_t)frame->arguments[0].kind);
        console_write(" arg0_flags=");
        console_write_hex64((uint64_t)frame->arguments[0].flags);
        console_write(" arg0_payload=");
        console_write_dec((uint64_t)frame->arguments[0].payload_size);
        console_write(" arg0_type=");
        console_write_hex64((uint64_t)frame->arguments[0].type_token_raw);
        console_write(" arg0_raw=");
        console_write_hex64((uint64_t)frame->arguments[0].data.raw);
    }
    console_write(" stack_depth=");
    console_write_dec((uint64_t)frame->eval_stack.depth);
    console_write(" len_offset=");
    console_write_dec((uint64_t)length_layout->byte_offset);
    console_write(" len_size=");
    console_write_dec((uint64_t)length_layout->field_size);
    console_write(" payload_size=");
    console_write_dec((uint64_t)payload_size);
    console_write(" result=");
    console_write_dec((uint64_t)result.status);
    console_write("\n");

    return result;
}

static struct zaclr_result invoke_memorymarshal_span_reference_intrinsic(
    struct zaclr_frame* frame,
    const struct zaclr_loaded_assembly* assembly,
    const struct zaclr_method_desc* method,
    bool require_non_empty)
{
    struct zaclr_stack_value receiver = {};
    uint32_t original_depth = 0u;
    struct zaclr_signature_type parameter_type = {};
    const struct zaclr_loaded_assembly* span_assembly = NULL;
    const struct zaclr_type_desc* span_type = NULL;
    const struct zaclr_field_layout* reference_layout;
    const struct zaclr_field_layout* length_layout;
    const uint8_t* payload;
    uint32_t payload_size = 0u;
    uint32_t payload_type_token = 0u;
    int32_t length = 0;
    struct zaclr_stack_value byref_value = {};
    struct zaclr_result result;

    if (frame == NULL || assembly == NULL || method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    original_depth = frame->eval_stack.depth;
    result = zaclr_eval_stack_pop(&frame->eval_stack, &receiver);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_signature_read_method_parameter(&method->signature, 0u, &parameter_type);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_type_system_resolve_type_desc(assembly,
                                                 frame->runtime,
                                                 parameter_type.type_token,
                                                 &span_assembly,
                                                 &span_type);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (span_type == NULL
        || span_type->type_namespace.text == NULL
        || span_type->type_name.text == NULL
        || !text_equals(span_type->type_namespace.text, "System")
        || (!text_equals(span_type->type_name.text, "Span`1")
            && !text_equals(span_type->type_name.text, "ReadOnlySpan`1")))
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    reference_layout = find_instance_field_layout_by_name(frame->runtime,
                                                          span_assembly != NULL ? span_assembly : assembly,
                                                          span_type,
                                                          "_reference");
    length_layout = find_instance_field_layout_by_name(frame->runtime,
                                                       span_assembly != NULL ? span_assembly : assembly,
                                                       span_type,
                                                       "_length");
    if (reference_layout == NULL
        || length_layout == NULL
        || reference_layout->field_size == 0u
        || length_layout->field_size == 0u
        || length_layout->field_size > sizeof(length))
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = try_read_span_payload(frame,
                                   &receiver,
                                   reference_layout,
                                   length_layout,
                                   &payload,
                                   &payload_size,
                                   &payload_type_token);
    if (result.status != ZACLR_STATUS_OK && frame->argument_count != 0u && frame->arguments != NULL)
    {
        result = try_read_span_payload(frame,
                                       &frame->arguments[0],
                                       reference_layout,
                                       length_layout,
                                       &payload,
                                       &payload_size,
                                       &payload_type_token);
    }

    if (result.status != ZACLR_STATUS_OK)
    {
        struct zaclr_result split_result = try_push_split_span_reference(frame,
                                                                        &receiver,
                                                                        payload_type_token,
                                                                        reference_layout->field_size,
                                                                        require_non_empty);
        if (split_result.status == ZACLR_STATUS_OK)
        {
            return split_result;
        }

        if (frame->eval_stack.depth + 1u == original_depth)
        {
            (void)zaclr_eval_stack_push(&frame->eval_stack, &receiver);
        }

        return result;
    }

    kernel_memcpy(&length, payload + length_layout->byte_offset, length_layout->field_size);
    if (require_non_empty && length == 0)
    {
        return zaclr_stack_value_set_byref(&byref_value,
                                           (void*)(uintptr_t)1u,
                                           reference_layout->field_size,
                                           payload_type_token,
                                           ZACLR_STACK_VALUE_FLAG_NONE).status == ZACLR_STATUS_OK
            ? zaclr_eval_stack_push(&frame->eval_stack, &byref_value)
            : zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    {
        uintptr_t referenced_address = 0u;
        kernel_memcpy(&referenced_address,
                      payload + reference_layout->byte_offset,
                      reference_layout->field_size < sizeof(referenced_address)
                          ? reference_layout->field_size
                          : sizeof(referenced_address));
        result = zaclr_stack_value_set_byref(&byref_value,
                                             (void*)referenced_address,
                                             reference_layout->field_size,
                                             payload_type_token,
                                             ZACLR_STACK_VALUE_FLAG_NONE);
    }
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_eval_stack_push(&frame->eval_stack, &byref_value);
}

static struct zaclr_result invoke_memorymarshal_get_non_null_pinnable_reference_intrinsic(
    struct zaclr_frame* frame,
    const struct zaclr_loaded_assembly* assembly,
    const struct zaclr_method_desc* method)
{
    return invoke_memorymarshal_span_reference_intrinsic(frame, assembly, method, true);
}

static struct zaclr_result invoke_memorymarshal_get_reference_intrinsic(
    struct zaclr_frame* frame,
    const struct zaclr_loaded_assembly* assembly,
    const struct zaclr_method_desc* method)
{
    return invoke_memorymarshal_span_reference_intrinsic(frame, assembly, method, false);
}

static struct zaclr_result invoke_bitconverter_bits_intrinsic(struct zaclr_frame* frame,
                                                              const struct zaclr_method_desc* method)
{
    struct zaclr_stack_value value = {};
    uint64_t bits;
    struct zaclr_result result;

    if (frame == NULL || method == NULL || method->name.text == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (text_equals(method->name.text, "Int32BitsToSingle") || text_equals(method->name.text, "UInt32BitsToSingle"))
    {
        return push_r4(frame, (uint32_t)numeric_stack_value_bits(&value, 4u));
    }

    if (text_equals(method->name.text, "Int64BitsToDouble") || text_equals(method->name.text, "UInt64BitsToDouble"))
    {
        return push_r8(frame, numeric_stack_value_bits(&value, 8u));
    }

    if (value.kind == ZACLR_STACK_VALUE_R4)
    {
        bits = value.data.r4_bits;
    }
    else if (value.kind == ZACLR_STACK_VALUE_R8)
    {
        bits = value.data.r8_bits;
    }
    else
    {
        bits = numeric_stack_value_bits(&value, 8u);
    }

    if (text_equals(method->name.text, "SingleToInt32Bits") || text_equals(method->name.text, "SingleToUInt32Bits"))
    {
        return push_i4(frame, (int32_t)(uint32_t)bits);
    }

    return push_i8(frame, (int64_t)bits);
}

static uint64_t reverse_endianness_bits(uint64_t bits, uint32_t width)
{
    switch (width)
    {
        case 2u:
            return (uint64_t)((((uint32_t)bits & 0x00FFu) << 8u) | (((uint32_t)bits & 0xFF00u) >> 8u));
        case 4u:
            return ((bits & 0x000000FFull) << 24u)
                 | ((bits & 0x0000FF00ull) << 8u)
                 | ((bits & 0x00FF0000ull) >> 8u)
                 | ((bits & 0xFF000000ull) >> 24u);
        case 8u:
            return ((bits & 0x00000000000000FFull) << 56u)
                 | ((bits & 0x000000000000FF00ull) << 40u)
                 | ((bits & 0x0000000000FF0000ull) << 24u)
                 | ((bits & 0x00000000FF000000ull) << 8u)
                 | ((bits & 0x000000FF00000000ull) >> 8u)
                 | ((bits & 0x0000FF0000000000ull) >> 24u)
                 | ((bits & 0x00FF000000000000ull) >> 40u)
                 | ((bits & 0xFF00000000000000ull) >> 56u);
        default:
            return bits;
    }
}

static struct zaclr_result invoke_binaryprimitives_reverseendianness_intrinsic(struct zaclr_frame* frame,
                                                                               const struct zaclr_method_desc* method)
{
    struct zaclr_stack_value value = {};
    uint32_t width;
    uint64_t bits;
    struct zaclr_result result;

    if (frame == NULL || method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    width = numeric_width_from_element_type(method->signature.return_type.element_type);
    if (width == 0u || width == 1u)
    {
        width = numeric_width_from_stack_value(&value);
    }

    if (width == 0u || width > 8u)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_EXEC);
    }

    bits = reverse_endianness_bits(numeric_stack_value_bits(&value, width), width);
    return width > 4u ? push_i8(frame, (int64_t)bits) : push_i4(frame, (int32_t)(uint32_t)bits);
}

static struct zaclr_result invoke_runtimehelpers_isknownconstant_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value ignored = {};
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &ignored);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return push_i4(frame, 0);
}

static struct zaclr_result invoke_gc_keepalive_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value ignored = {};

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    return zaclr_eval_stack_pop(&frame->eval_stack, &ignored);
}

static struct zaclr_result invoke_runtimehelpers_isbitwiseequatable_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value generic_owner = {};
    const struct zaclr_method_table* method_table = NULL;
    uint8_t result_value = 0u;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    /* CoreCLR does not execute the placeholder IL body for RuntimeHelpers.IsBitwiseEquatable<T>().
       It recognizes the exact RuntimeHelpers method identity in vm/corelib.h and replaces the body in
       vm/jitinterface.cpp:getILIntrinsicImplementationForRuntimeHelpers(). ZACLR mirrors that policy here
       in the interpreter rather than allowing fallback to the dummy managed body.

       Current conservative rule set:
       - primitive scalar value types (1/2/4/8-byte, pointer-sized, char, bool) => true
       - enums => true
       - any reference type / array / string / component-sized type / type containing references => false
       - other value types => false for now until ZACLR gains a full CanCompareBits-style analysis

       This keeps the semantics safe and avoids routing Array.LastIndexOf / MemoryExtensions into the JIT-
       only NotSupported-style path when the runtime is interpreter-first. */
    {
        struct zaclr_result pop_result = zaclr_eval_stack_pop(&frame->eval_stack, &generic_owner);
        if (pop_result.status == ZACLR_STATUS_OK)
        {
            if (generic_owner.kind == ZACLR_STACK_VALUE_I8)
            {
                method_table = (const struct zaclr_method_table*)(uintptr_t)generic_owner.data.i8;
            }
            else if (generic_owner.kind == ZACLR_STACK_VALUE_I4)
            {
                method_table = (const struct zaclr_method_table*)(uintptr_t)(uint32_t)generic_owner.data.i4;
            }
        }
    }

    if (method_table != NULL)
    {
        if (zaclr_method_table_is_enum(method_table) != 0u)
        {
            result_value = 1u;
        }
        else if (zaclr_method_table_is_value_type(method_table) != 0u
                 && zaclr_method_table_contains_references(method_table) == 0u
                 && zaclr_method_table_component_size(method_table) == 0u)
        {
            uint32_t payload_size = zaclr_method_table_instance_size(method_table);
            if (payload_size >= (uint32_t)sizeof(struct zaclr_object_desc))
            {
                payload_size -= (uint32_t)sizeof(struct zaclr_object_desc);
            }
            else
            {
                payload_size = 0u;
            }

            if (payload_size == 1u
                || payload_size == 2u
                || payload_size == 4u
                || payload_size == 8u
                || payload_size == sizeof(uintptr_t))
            {
                result_value = 1u;
            }
        }
    }

    return push_i4(frame, result_value != 0u ? 1 : 0);
}

static uint8_t generic_argument_is_reference_or_contains_references(struct zaclr_runtime* runtime,
                                                                    const struct zaclr_loaded_assembly* fallback_assembly,
                                                                    const struct zaclr_generic_argument* argument)
{
    const struct zaclr_loaded_assembly* argument_assembly;
    const struct zaclr_type_desc* type_desc = NULL;
    const struct zaclr_loaded_assembly* resolved_assembly = NULL;
    struct zaclr_method_table* method_table = NULL;

    if (argument == NULL)
    {
        return 0u;
    }

    if (argument->kind == ZACLR_GENERIC_ARGUMENT_KIND_PRIMITIVE)
    {
        return argument->element_type == ZACLR_ELEMENT_TYPE_STRING
            || argument->element_type == ZACLR_ELEMENT_TYPE_OBJECT
            || argument->element_type == ZACLR_ELEMENT_TYPE_CLASS;
    }

    if (argument->element_type == ZACLR_ELEMENT_TYPE_CLASS)
    {
        return 1u;
    }

    if (argument->element_type != ZACLR_ELEMENT_TYPE_VALUETYPE)
    {
        return argument->kind == ZACLR_GENERIC_ARGUMENT_KIND_GENERIC_INST ? 1u : 0u;
    }

    argument_assembly = argument->assembly != NULL ? argument->assembly : fallback_assembly;
    if (runtime == NULL || argument_assembly == NULL || zaclr_token_is_nil(&argument->token))
    {
        return 0u;
    }

    if (zaclr_type_system_resolve_type_desc(argument_assembly,
                                            runtime,
                                            argument->token,
                                            &resolved_assembly,
                                            &type_desc).status != ZACLR_STATUS_OK
        || resolved_assembly == NULL
        || type_desc == NULL)
    {
        return 0u;
    }

    if (zaclr_type_prepare(runtime,
                           (struct zaclr_loaded_assembly*)resolved_assembly,
                           type_desc,
                           &method_table).status != ZACLR_STATUS_OK
        || method_table == NULL)
    {
        return 0u;
    }

    return zaclr_method_table_contains_references(method_table) != 0u ? 1u : 0u;
}

static struct zaclr_result invoke_runtimehelpers_isreferenceorcontainsreferences_intrinsic(struct zaclr_frame* frame)
{
    const struct zaclr_generic_argument* method_argument;
    uint8_t result_value;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    method_argument = zaclr_generic_context_get_method_argument(&frame->generic_context, 0u);
    result_value = generic_argument_is_reference_or_contains_references(frame->runtime,
                                                                        frame->assembly,
                                                                        method_argument);
    return push_i4(frame, result_value != 0u ? 1 : 0);
}

static struct zaclr_result invoke_runtimehelpers_getmethodtable_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_object_desc* object;
    struct zaclr_result result;

    if (frame == NULL || frame->runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        struct zaclr_stack_value* target = resolve_local_address_target(frame, &value);
        if (target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        value = *target;
    }

    if (value.kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    object = value.data.object_reference;
    if (object == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    {
        const struct zaclr_method_table* method_table = zaclr_object_method_table_const(object);
        return push_i8(frame, method_table != NULL ? (int64_t)(uintptr_t)method_table : 0);
    }
}

static struct zaclr_result invoke_runtimehelpers_objecthascomponentsize_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_object_desc* object;
    const struct zaclr_method_table* method_table = NULL;
    struct zaclr_result result;

    if (frame == NULL || frame->runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        struct zaclr_stack_value* target = resolve_local_address_target(frame, &value);
        if (target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        value = *target;
    }

    if (value.kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    object = value.data.object_reference;
    if (object == NULL)
    {
        return push_i4(frame, 0);
    }

    method_table = zaclr_object_method_table_const(object);
    return push_i4(frame,
                   method_table != NULL && zaclr_method_table_component_size(method_table) != 0u ? 1 : 0);
}

static struct zaclr_result invoke_methodtable_hasfinalizer_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value method_table_value = {};
    const struct zaclr_method_table* method_table = NULL;
    struct zaclr_result result;

    if (frame == NULL || frame->runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &method_table_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (method_table_value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        struct zaclr_stack_value* target = resolve_local_address_target(frame, &method_table_value);
        if (target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        method_table_value = *target;
    }

    if (method_table_value.kind == ZACLR_STACK_VALUE_I8)
    {
        method_table = (const struct zaclr_method_table*)(uintptr_t)method_table_value.data.i8;
    }
    else if (method_table_value.kind != ZACLR_STACK_VALUE_I4)
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (method_table == NULL)
    {
        return push_i4(frame, 0);
    }

    return push_i4(frame, zaclr_method_table_has_finalizer(method_table) != 0u ? 1 : 0);
}

static struct zaclr_result invoke_methodtable_hascomponentsize_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value method_table_value = {};
    const struct zaclr_method_table* method_table = NULL;
    struct zaclr_result result;

    if (frame == NULL || frame->runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &method_table_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (method_table_value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        struct zaclr_stack_value* target = resolve_local_address_target(frame, &method_table_value);
        if (target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        method_table_value = *target;
    }

    if (method_table_value.kind == ZACLR_STACK_VALUE_I8)
    {
        method_table = (const struct zaclr_method_table*)(uintptr_t)method_table_value.data.i8;
    }
    else if (method_table_value.kind != ZACLR_STACK_VALUE_I4)
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (method_table == NULL)
    {
        return push_i4(frame, 0);
    }

    return push_i4(frame, zaclr_method_table_component_size(method_table) != 0u ? 1 : 0);
}

static uint32_t interlocked_width_from_method(const struct zaclr_method_desc* method,
                                              const struct zaclr_stack_value* address,
                                              const struct zaclr_stack_value* value)
{
    uint32_t width = address != NULL && address->payload_size != 0u && address->payload_size <= 8u
        ? address->payload_size
        : 0u;

    if (width == 0u && method != NULL)
    {
        switch (method->signature.return_type.element_type)
        {
            case ZACLR_ELEMENT_TYPE_I1:
            case ZACLR_ELEMENT_TYPE_U1:
            case ZACLR_ELEMENT_TYPE_BOOLEAN:
                width = 1u;
                break;
            case ZACLR_ELEMENT_TYPE_I2:
            case ZACLR_ELEMENT_TYPE_U2:
            case ZACLR_ELEMENT_TYPE_CHAR:
                width = 2u;
                break;
            case ZACLR_ELEMENT_TYPE_I8:
            case ZACLR_ELEMENT_TYPE_U8:
            case ZACLR_ELEMENT_TYPE_I:
            case ZACLR_ELEMENT_TYPE_U:
                width = 8u;
                break;
            case ZACLR_ELEMENT_TYPE_CLASS:
            case ZACLR_ELEMENT_TYPE_OBJECT:
            case ZACLR_ELEMENT_TYPE_STRING:
                width = sizeof(void*);
                break;
            default:
                width = 4u;
                break;
        }
    }

    if (width == 0u && value != NULL)
    {
        if (value->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        {
            width = sizeof(void*);
        }
        else if (value->kind == ZACLR_STACK_VALUE_I8 || value->kind == ZACLR_STACK_VALUE_R8)
        {
            width = 8u;
        }
        else if (value->payload_size != 0u && value->payload_size <= 8u)
        {
            width = value->payload_size;
        }
        else
        {
            width = 4u;
        }
    }

    return width == 0u ? 4u : width;
}

static uint64_t interlocked_stack_value_bits(const struct zaclr_stack_value* value,
                                             uint32_t width)
{
    uint64_t bits = 0u;
    const void* payload;

    if (value == NULL)
    {
        return 0u;
    }

    switch (value->kind)
    {
        case ZACLR_STACK_VALUE_OBJECT_REFERENCE:
            return (uint64_t)(uintptr_t)value->data.object_reference;
        case ZACLR_STACK_VALUE_I8:
            return (uint64_t)value->data.i8;
        case ZACLR_STACK_VALUE_R8:
            return value->data.r8_bits;
        case ZACLR_STACK_VALUE_R4:
            return (uint64_t)value->data.r4_bits;
        case ZACLR_STACK_VALUE_I4:
            return (uint64_t)(uint32_t)value->data.i4;
        case ZACLR_STACK_VALUE_VALUETYPE:
            payload = zaclr_stack_value_payload_const(value);
            if (payload != NULL)
            {
                kernel_memcpy(&bits, payload, value->payload_size < width ? value->payload_size : width);
            }
            return bits;
        default:
            return (uint64_t)value->data.raw;
    }
}

static struct zaclr_result interlocked_push_bits(struct zaclr_frame* frame,
                                                 const struct zaclr_method_desc* method,
                                                 uint32_t width,
                                                 uint64_t bits)
{
    if (method != NULL)
    {
        switch (method->signature.return_type.element_type)
        {
            case ZACLR_ELEMENT_TYPE_VOID:
                return zaclr_result_ok();
            case ZACLR_ELEMENT_TYPE_CLASS:
            case ZACLR_ELEMENT_TYPE_OBJECT:
            case ZACLR_ELEMENT_TYPE_STRING:
                return push_object_reference(frame, (struct zaclr_object_desc*)(uintptr_t)bits);
            case ZACLR_ELEMENT_TYPE_I8:
            case ZACLR_ELEMENT_TYPE_U8:
            case ZACLR_ELEMENT_TYPE_I:
            case ZACLR_ELEMENT_TYPE_U:
                return push_i8(frame, (int64_t)bits);
            default:
                break;
        }
    }

    return width > 4u ? push_i8(frame, (int64_t)bits)
                      : push_i4(frame, (int32_t)(uint32_t)bits);
}

static uint64_t interlocked_read_raw_bits(const void* raw_address, uint32_t width)
{
    uint64_t bits = 0u;
    if (raw_address != NULL)
    {
        kernel_memcpy(&bits, raw_address, width < sizeof(bits) ? width : sizeof(bits));
    }
    return bits;
}

static void interlocked_write_raw_bits(void* raw_address, uint32_t width, uint64_t bits)
{
    if (raw_address != NULL)
    {
        kernel_memcpy(raw_address, &bits, width < sizeof(bits) ? width : sizeof(bits));
    }
}

static struct zaclr_result interlocked_store_stack_slot(struct zaclr_stack_value* target,
                                                        const struct zaclr_stack_value* template_value,
                                                        uint32_t width,
                                                        uint64_t bits)
{
    if (target == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if ((target->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        || (template_value != NULL && template_value->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE))
    {
        target->kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
        target->data.object_reference = (struct zaclr_object_desc*)(uintptr_t)bits;
        return zaclr_result_ok();
    }

    if (target->kind == ZACLR_STACK_VALUE_VALUETYPE)
    {
        void* payload = zaclr_stack_value_payload(target);
        if (payload == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        kernel_memcpy(payload, &bits, target->payload_size < width ? target->payload_size : width);
        return zaclr_result_ok();
    }

    if (width > 4u || target->kind == ZACLR_STACK_VALUE_I8 || (template_value != NULL && template_value->kind == ZACLR_STACK_VALUE_I8))
    {
        target->kind = ZACLR_STACK_VALUE_I8;
        target->data.i8 = (int64_t)bits;
        return zaclr_result_ok();
    }

    target->kind = ZACLR_STACK_VALUE_I4;
    target->data.i4 = (int32_t)(uint32_t)bits;
    return zaclr_result_ok();
}

static bool interlocked_first_parameter_is_pointer(const struct zaclr_method_desc* method)
{
    struct zaclr_signature_type parameter = {};

    if (method == NULL || method->signature.parameter_count == 0u)
    {
        return false;
    }

    if (zaclr_signature_read_method_parameter(&method->signature, 0u, &parameter).status != ZACLR_STATUS_OK)
    {
        return false;
    }

    return parameter.element_type == ZACLR_ELEMENT_TYPE_PTR;
}

static struct zaclr_result interlocked_resolve_target(struct zaclr_frame* frame,
                                                      const struct zaclr_method_desc* method,
                                                      struct zaclr_stack_value* address,
                                                      struct zaclr_stack_value** out_stack_slot,
                                                      void** out_raw_address)
{
    if (address == NULL || out_stack_slot == NULL || out_raw_address == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    *out_stack_slot = NULL;
    *out_raw_address = NULL;

    if (address->kind == ZACLR_STACK_VALUE_BYREF
        && (address->flags & ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT) == 0u)
    {
        *out_raw_address = (void*)(uintptr_t)address->data.raw;
        return *out_raw_address != NULL
            ? zaclr_result_ok()
            : zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (interlocked_first_parameter_is_pointer(method)
        && address->kind == ZACLR_STACK_VALUE_I8
        && address->data.i8 != 0)
    {
        *out_raw_address = (void*)(uintptr_t)address->data.i8;
        return zaclr_result_ok();
    }

    if (interlocked_first_parameter_is_pointer(method)
        && address->kind == ZACLR_STACK_VALUE_I4
        && address->data.i4 != 0)
    {
        *out_raw_address = (void*)(uintptr_t)(uint32_t)address->data.i4;
        return zaclr_result_ok();
    }

    *out_stack_slot = resolve_local_address_target(frame, address);
    return *out_stack_slot != NULL
        ? zaclr_result_ok()
        : zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
}

enum interlocked_binary_operation {
    INTERLOCKED_BINARY_ADD,
    INTERLOCKED_BINARY_AND,
    INTERLOCKED_BINARY_OR
};

static uint64_t interlocked_apply_binary_operation(enum interlocked_binary_operation operation,
                                                   uint64_t current,
                                                   uint64_t value)
{
    switch (operation)
    {
        case INTERLOCKED_BINARY_ADD:
            return current + value;
        case INTERLOCKED_BINARY_AND:
            return current & value;
        case INTERLOCKED_BINARY_OR:
            return current | value;
        default:
            return current;
    }
}

static struct zaclr_result invoke_interlocked_exchange_intrinsic(struct zaclr_frame* frame,
                                                                const struct zaclr_method_desc* method)
{
    struct zaclr_stack_value value = {};
    struct zaclr_stack_value address = {};
    struct zaclr_stack_value* target = NULL;
    void* raw_address = NULL;
    uint32_t width;
    uint64_t previous_bits;
    uint64_t value_bits;
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = interlocked_resolve_target(frame, method, &address, &target, &raw_address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    width = interlocked_width_from_method(method, &address, &value);
    value_bits = interlocked_stack_value_bits(&value, width);

    if (raw_address != NULL)
    {
        previous_bits = interlocked_read_raw_bits(raw_address, width);
        interlocked_write_raw_bits(raw_address, width, value_bits);
        return interlocked_push_bits(frame, method, width, previous_bits);
    }

    previous_bits = interlocked_stack_value_bits(target, width);
    result = interlocked_store_stack_slot(target, &value, width, value_bits);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return interlocked_push_bits(frame, method, width, previous_bits);
}

static struct zaclr_result invoke_interlocked_compareexchange_intrinsic(struct zaclr_frame* frame,
                                                                       const struct zaclr_method_desc* method)
{
    struct zaclr_stack_value comparand = {};
    struct zaclr_stack_value value = {};
    struct zaclr_stack_value address = {};
    struct zaclr_stack_value* target = NULL;
    void* raw_address = NULL;
    uint32_t width;
    uint64_t previous_bits;
    uint64_t value_bits;
    uint64_t comparand_bits;
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &comparand);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = interlocked_resolve_target(frame, method, &address, &target, &raw_address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    width = interlocked_width_from_method(method, &address, &value);
    value_bits = interlocked_stack_value_bits(&value, width);
    comparand_bits = interlocked_stack_value_bits(&comparand, width);

    if (raw_address != NULL)
    {
        previous_bits = interlocked_read_raw_bits(raw_address, width);
        if (previous_bits == comparand_bits)
        {
            interlocked_write_raw_bits(raw_address, width, value_bits);
        }
        return interlocked_push_bits(frame, method, width, previous_bits);
    }

    previous_bits = interlocked_stack_value_bits(target, width);
    if (previous_bits == comparand_bits)
    {
        result = interlocked_store_stack_slot(target, &value, width, value_bits);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
    }

    return interlocked_push_bits(frame, method, width, previous_bits);
}

static struct zaclr_result invoke_interlocked_binary_intrinsic(struct zaclr_frame* frame,
                                                              const struct zaclr_method_desc* method,
                                                              enum interlocked_binary_operation operation,
                                                              bool return_new_value)
{
    struct zaclr_stack_value value = {};
    struct zaclr_stack_value address = {};
    struct zaclr_stack_value* target = NULL;
    void* raw_address = NULL;
    uint32_t width;
    uint64_t previous_bits;
    uint64_t value_bits;
    uint64_t new_bits;
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = interlocked_resolve_target(frame, method, &address, &target, &raw_address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    width = interlocked_width_from_method(method, &address, &value);
    value_bits = interlocked_stack_value_bits(&value, width);

    if (raw_address != NULL)
    {
        previous_bits = interlocked_read_raw_bits(raw_address, width);
        new_bits = interlocked_apply_binary_operation(operation, previous_bits, value_bits);
        interlocked_write_raw_bits(raw_address, width, new_bits);
        return interlocked_push_bits(frame, method, width, return_new_value ? new_bits : previous_bits);
    }

    previous_bits = interlocked_stack_value_bits(target, width);
    new_bits = interlocked_apply_binary_operation(operation, previous_bits, value_bits);
    result = interlocked_store_stack_slot(target, &value, width, new_bits);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return interlocked_push_bits(frame, method, width, return_new_value ? new_bits : previous_bits);
}

static struct zaclr_result invoke_interlocked_increment_or_decrement_intrinsic(struct zaclr_frame* frame,
                                                                              const struct zaclr_method_desc* method,
                                                                              int64_t delta)
{
    struct zaclr_stack_value address = {};
    struct zaclr_stack_value* target = NULL;
    void* raw_address = NULL;
    uint32_t width;
    uint64_t previous_bits;
    uint64_t new_bits;
    struct zaclr_stack_value template_value = {};
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = interlocked_resolve_target(frame, method, &address, &target, &raw_address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    width = interlocked_width_from_method(method, &address, target);
    if (raw_address != NULL)
    {
        previous_bits = interlocked_read_raw_bits(raw_address, width);
        new_bits = (uint64_t)((int64_t)previous_bits + delta);
        interlocked_write_raw_bits(raw_address, width, new_bits);
        return interlocked_push_bits(frame, method, width, new_bits);
    }

    previous_bits = interlocked_stack_value_bits(target, width);
    new_bits = (uint64_t)((int64_t)previous_bits + delta);
    template_value.kind = width > 4u ? ZACLR_STACK_VALUE_I8 : ZACLR_STACK_VALUE_I4;
    result = interlocked_store_stack_slot(target, &template_value, width, new_bits);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return interlocked_push_bits(frame, method, width, new_bits);
}

static struct zaclr_result invoke_interlocked_read_intrinsic(struct zaclr_frame* frame,
                                                             const struct zaclr_method_desc* method)
{
    struct zaclr_stack_value address = {};
    struct zaclr_stack_value* target = NULL;
    void* raw_address = NULL;
    uint32_t width;
    uint64_t bits;
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = interlocked_resolve_target(frame, method, &address, &target, &raw_address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    width = interlocked_width_from_method(method, &address, target);
    bits = raw_address != NULL
        ? interlocked_read_raw_bits(raw_address, width)
        : interlocked_stack_value_bits(target, width);
    return interlocked_push_bits(frame, method, width, bits);
}

static uint32_t volatile_width_from_values(const struct zaclr_method_desc* method,
                                           const struct zaclr_stack_value* address,
                                           const struct zaclr_stack_value* target_or_value)
{
    if (method != NULL
        && (method->signature.return_type.element_type == ZACLR_ELEMENT_TYPE_VAR
            || method->signature.return_type.element_type == ZACLR_ELEMENT_TYPE_MVAR)
        && target_or_value != NULL)
    {
        uint32_t width = stack_value_scalar_width(target_or_value);
        if (width != 0u && width <= 8u)
        {
            return width;
        }
    }

    return interlocked_width_from_method(method, address, target_or_value);
}

static struct zaclr_result invoke_volatile_read_intrinsic(struct zaclr_frame* frame,
                                                          const struct zaclr_method_desc* method)
{
    struct zaclr_stack_value address = {};
    struct zaclr_stack_value* target = NULL;
    void* raw_address = NULL;
    uint32_t width;
    uint64_t bits;
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = interlocked_resolve_target(frame, method, &address, &target, &raw_address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    width = volatile_width_from_values(method, &address, target);
    bits = raw_address != NULL
        ? interlocked_read_raw_bits(raw_address, width)
        : interlocked_stack_value_bits(target, width);
    return interlocked_push_bits(frame, method, width, bits);
}

static struct zaclr_result invoke_volatile_write_intrinsic(struct zaclr_frame* frame,
                                                           const struct zaclr_method_desc* method)
{
    struct zaclr_stack_value value = {};
    struct zaclr_stack_value address = {};
    struct zaclr_stack_value* target = NULL;
    void* raw_address = NULL;
    uint32_t width;
    uint64_t bits;
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = interlocked_resolve_target(frame, method, &address, &target, &raw_address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    width = volatile_width_from_values(method, &address, &value);
    bits = interlocked_stack_value_bits(&value, width);
    if (raw_address != NULL)
    {
        interlocked_write_raw_bits(raw_address, width, bits);
        return zaclr_result_ok();
    }

    return interlocked_store_stack_slot(target, &value, width, bits);
}

static struct zaclr_result invoke_gchandle_gethandlevalue_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_result result;
    const void* payload = NULL;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (value.kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
    {
        struct zaclr_stack_value* target = resolve_local_address_target(frame, &value);
        if (target == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
        }

        value = *target;
    }

    if (value.kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE && value.data.object_reference != NULL)
    {
        const struct zaclr_boxed_value_desc* boxed_value = (const struct zaclr_boxed_value_desc*)value.data.object_reference;
        if ((zaclr_object_flags(&boxed_value->object) & ZACLR_OBJECT_FLAG_BOXED_VALUE) != 0u)
        {
            value = boxed_value->value;
        }
    }

    if (value.kind == ZACLR_STACK_VALUE_I8)
    {
        return push_i8(frame, value.data.i8 & ~1ll);
    }

    if (value.kind == ZACLR_STACK_VALUE_VALUETYPE)
    {
        payload = zaclr_stack_value_payload_const(&value);
        if (payload == NULL || value.payload_size == 0u)
        {
            return push_i4(frame, 0);
        }

        if (value.payload_size >= sizeof(int64_t))
        {
            int64_t raw = 0;
            const uint8_t* bytes = (const uint8_t*)payload;
            for (uint32_t index = 0u; index < sizeof(int64_t); ++index)
            {
                raw |= ((int64_t)bytes[index]) << (index * 8u);
            }

            return push_i8(frame, raw & ~1ll);
        }

        if (value.payload_size >= sizeof(int32_t))
        {
            int32_t raw = 0;
            const uint8_t* bytes = (const uint8_t*)payload;
            for (uint32_t index = 0u; index < sizeof(int32_t); ++index)
            {
                raw |= (int32_t)((uint32_t)bytes[index] << (index * 8u));
            }

            return push_i4(frame, raw & ~1);
        }

        return push_i4(frame, 0);
    }

    return push_i4(frame, value.data.i4 & ~1);
}

static struct zaclr_result invoke_objecthandleonstack_create_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value value = {};
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        if (frame->eval_stack.depth == 0u && frame->local_count > 0u)
        {
            struct zaclr_stack_value synthetic = {};
            result = zaclr_stack_value_set_byref(&synthetic,
                                                 &frame->locals[0],
                                                 sizeof(struct zaclr_stack_value),
                                                 0u,
                                                 ZACLR_STACK_VALUE_FLAG_BYREF_STACK_SLOT);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            return zaclr_eval_stack_push(&frame->eval_stack, &synthetic);
        }

        return result;
    }

    return zaclr_eval_stack_push(&frame->eval_stack, &value);
}

static struct zaclr_result invoke_monitor_enter_intrinsic(struct zaclr_frame* frame)
{
    struct zaclr_stack_value lock_taken_address = {};
    struct zaclr_stack_value object_value = {};
    struct zaclr_stack_value* target = NULL;
    struct zaclr_result result;

    if (frame == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &lock_taken_address);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_eval_stack_pop(&frame->eval_stack, &object_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (object_value.kind != ZACLR_STACK_VALUE_OBJECT_REFERENCE || object_value.data.object_reference == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    target = resolve_local_address_target(frame, &lock_taken_address);
    if (target == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    target->kind = ZACLR_STACK_VALUE_I4;
    target->data.i4 = 1;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_try_invoke_intrinsic(struct zaclr_frame* frame,
                                                           const struct zaclr_loaded_assembly* assembly,
                                                           const struct zaclr_type_desc* type,
                                                           const struct zaclr_method_desc* method)
{
    const struct zaclr_type_desc* effective_type = type;

    (void)assembly;

    if (effective_type == NULL && assembly != NULL && method != NULL)
    {
        effective_type = zaclr_type_map_find_by_token(&assembly->type_map, method->owning_type_token);
    }

    if (is_unsafe_as_intrinsic(effective_type, method))
    {
        return invoke_unsafe_as_intrinsic(frame, method);
    }

    if (is_unsafe_aspointer_intrinsic(effective_type, method))
    {
        return invoke_unsafe_aspointer_intrinsic(frame);
    }

    if (is_unsafe_add_intrinsic(effective_type, method))
    {
        return invoke_unsafe_add_intrinsic(frame);
    }

    if (is_unsafe_named_intrinsic(effective_type, method, "AddByteOffset"))
    {
        return invoke_unsafe_byte_offset_intrinsic(frame, 1);
    }

    if (is_unsafe_named_intrinsic(effective_type, method, "SubtractByteOffset"))
    {
        return invoke_unsafe_byte_offset_intrinsic(frame, -1);
    }

    if (is_unsafe_named_intrinsic(effective_type, method, "AreSame"))
    {
        return invoke_unsafe_are_same_or_offset_intrinsic(frame, false);
    }

    if (is_unsafe_named_intrinsic(effective_type, method, "ByteOffset"))
    {
        return invoke_unsafe_are_same_or_offset_intrinsic(frame, true);
    }

    if (is_unsafe_named_intrinsic(effective_type, method, "NullRef"))
    {
        return invoke_unsafe_nullref_intrinsic(frame);
    }

    if (is_unsafe_named_intrinsic(effective_type, method, "IsNullRef"))
    {
        return invoke_unsafe_isnullref_intrinsic(frame);
    }

    if (is_unsafe_named_intrinsic(effective_type, method, "AsRef"))
    {
        return invoke_unsafe_asref_intrinsic(frame);
    }

    if (is_unsafe_named_intrinsic(effective_type, method, "SkipInit"))
    {
        return invoke_unsafe_skipinit_intrinsic(frame);
    }

    if (is_unsafe_named_intrinsic(effective_type, method, "ReadUnaligned"))
    {
        return invoke_unsafe_read_unaligned_intrinsic(frame, method);
    }

    if (is_unsafe_named_intrinsic(effective_type, method, "WriteUnaligned"))
    {
        return invoke_unsafe_write_unaligned_intrinsic(frame, method);
    }

    if (is_vector_ishardwareaccelerated_intrinsic(effective_type, method)
        || is_hardwareintrinsic_issupported_intrinsic(effective_type, method))
    {
        return invoke_vector_ishardwareaccelerated_intrinsic(frame);
    }

    if (is_inumberbase_isnegative_intrinsic(effective_type, method))
    {
        return invoke_inumberbase_isnegative_intrinsic(frame);
    }

    {
        zaclr_string_intrinsic_kind string_intrinsic_kind = zaclr_string_intrinsic_kind::none;
        if (try_get_string_runtime_intrinsic_kind(effective_type, method, &string_intrinsic_kind))
        {
            return invoke_string_intrinsic(frame, string_intrinsic_kind);
        }
    }

    if (is_span_get_length_intrinsic(effective_type, method))
    {
        return invoke_span_get_length_intrinsic(frame, assembly, effective_type);
    }

    if (is_memorymarshal_get_non_null_pinnable_reference_intrinsic(effective_type, method))
    {
        return invoke_memorymarshal_get_non_null_pinnable_reference_intrinsic(frame, assembly, method);
    }

    if (is_memorymarshal_get_reference_intrinsic(effective_type, method))
    {
        return invoke_memorymarshal_get_reference_intrinsic(frame, assembly, method);
    }

    if (is_bitconverter_bits_intrinsic(effective_type, method))
    {
        return invoke_bitconverter_bits_intrinsic(frame, method);
    }

    if (is_binaryprimitives_reverseendianness_intrinsic(effective_type, method))
    {
        return invoke_binaryprimitives_reverseendianness_intrinsic(frame, method);
    }

    if (is_runtimehelpers_getmethodtable_intrinsic(effective_type, method))
    {
        return invoke_runtimehelpers_getmethodtable_intrinsic(frame);
    }

    if (is_runtimehelpers_objecthascomponentsize_intrinsic(effective_type, method))
    {
        return invoke_runtimehelpers_objecthascomponentsize_intrinsic(frame);
    }

    if (is_runtimehelpers_isbitwiseequatable_intrinsic(effective_type, method))
    {
        return invoke_runtimehelpers_isbitwiseequatable_intrinsic(frame);
    }

    if (is_runtimehelpers_isreferenceorcontainsreferences_intrinsic(effective_type, method))
    {
        return invoke_runtimehelpers_isreferenceorcontainsreferences_intrinsic(frame);
    }

    if (is_runtimehelpers_isknownconstant_intrinsic(effective_type, method))
    {
        return invoke_runtimehelpers_isknownconstant_intrinsic(frame);
    }

    if (is_gc_keepalive_intrinsic(effective_type, method))
    {
        return invoke_gc_keepalive_intrinsic(frame);
    }

    if (is_methodtable_hasfinalizer_intrinsic(effective_type, method))
    {
        return invoke_methodtable_hasfinalizer_intrinsic(frame);
    }

    if (is_methodtable_hascomponentsize_intrinsic(effective_type, method))
    {
        return invoke_methodtable_hascomponentsize_intrinsic(frame);
    }

    if (is_interlocked_exchange_intrinsic(effective_type, method))
    {
        console_write("[ZACLR][intrinsic] matched Interlocked.Exchange ns=");
        console_write(effective_type != NULL && effective_type->type_namespace.text != NULL ? effective_type->type_namespace.text : "<null>");
        console_write(" type=");
        console_write(effective_type != NULL && effective_type->type_name.text != NULL ? effective_type->type_name.text : "<null>");
        console_write(" method=");
        console_write(method != NULL && method->name.text != NULL ? method->name.text : "<null>");
        console_write(" params=");
        console_write_dec((uint64_t)(method != NULL ? method->signature.parameter_count : 0u));
        console_write(" depth=");
        console_write_dec((uint64_t)(frame != NULL ? frame->eval_stack.depth : 0u));
        console_write("\n");
        return invoke_interlocked_exchange_intrinsic(frame, method);
    }

    if (is_interlocked_compareexchange_intrinsic(effective_type, method))
    {
        return invoke_interlocked_compareexchange_intrinsic(frame, method);
    }

    if (is_interlocked_exchangeadd_intrinsic(effective_type, method))
    {
        return invoke_interlocked_binary_intrinsic(frame, method, INTERLOCKED_BINARY_ADD, false);
    }

    if (is_interlocked_add_intrinsic(effective_type, method))
    {
        return invoke_interlocked_binary_intrinsic(frame, method, INTERLOCKED_BINARY_ADD, true);
    }

    if (is_interlocked_and_intrinsic(effective_type, method))
    {
        return invoke_interlocked_binary_intrinsic(frame, method, INTERLOCKED_BINARY_AND, false);
    }

    if (is_interlocked_or_intrinsic(effective_type, method))
    {
        return invoke_interlocked_binary_intrinsic(frame, method, INTERLOCKED_BINARY_OR, false);
    }

    if (is_interlocked_increment_intrinsic(effective_type, method))
    {
        return invoke_interlocked_increment_or_decrement_intrinsic(frame, method, 1);
    }

    if (is_interlocked_decrement_intrinsic(effective_type, method))
    {
        return invoke_interlocked_increment_or_decrement_intrinsic(frame, method, -1);
    }

    if (is_interlocked_read_intrinsic(effective_type, method))
    {
        return invoke_interlocked_read_intrinsic(frame, method);
    }

    if (is_interlocked_memorybarrier_intrinsic(effective_type, method))
    {
        return zaclr_result_ok();
    }

    if (is_volatile_read_intrinsic(effective_type, method))
    {
        return invoke_volatile_read_intrinsic(frame, method);
    }

    if (is_volatile_write_intrinsic(effective_type, method))
    {
        return invoke_volatile_write_intrinsic(frame, method);
    }

    if (is_volatile_barrier_intrinsic(effective_type, method))
    {
        return zaclr_result_ok();
    }

    if (is_gchandle_gethandlevalue_intrinsic(effective_type, method))
    {
        return invoke_gchandle_gethandlevalue_intrinsic(frame);
    }

    if (is_objecthandleonstack_create_intrinsic(effective_type, method))
    {
        console_write("[ZACLR][intrinsic] matched ObjectHandleOnStack.Create depth=");
        console_write_dec((uint64_t)(frame != NULL ? frame->eval_stack.depth : 0u));
        console_write(" type_ns=");
        console_write(effective_type != NULL && effective_type->type_namespace.text != NULL ? effective_type->type_namespace.text : "<null>");
        console_write(" type=");
        console_write(effective_type != NULL && effective_type->type_name.text != NULL ? effective_type->type_name.text : "<null>");
        console_write("\n");
        return invoke_objecthandleonstack_create_intrinsic(frame);
    }

    if (is_monitor_enter_intrinsic(effective_type, method))
    {
        return invoke_monitor_enter_intrinsic(frame);
    }

    if (is_memberinfo_get_module_intrinsic(effective_type, method))
    {
        return invoke_memberinfo_get_module_intrinsic(frame);
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
}
