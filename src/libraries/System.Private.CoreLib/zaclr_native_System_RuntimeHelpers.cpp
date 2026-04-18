#include "zaclr_native_System_RuntimeHelpers.h"

#include <kernel/zaclr/exec/zaclr_engine.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>
#include <kernel/zaclr/metadata/zaclr_type_map.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_method_table.h>
#include <kernel/support/kernel_memory.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static const struct zaclr_method_desc* find_type_initializer_method(const struct zaclr_loaded_assembly* assembly,
                                                                        const struct zaclr_type_desc* type)
    {
        if (assembly == NULL || type == NULL || assembly->method_map.methods == NULL)
        {
            return NULL;
        }

        for (uint32_t method_index = 0u; method_index < type->method_count; ++method_index)
        {
            const struct zaclr_method_desc* method = &assembly->method_map.methods[type->first_method_index + method_index];
            if (method->name.text != NULL
                && zaclr_internal_call_text_equals(method->name.text, ".cctor")
                && method->signature.parameter_count == 0u
                && (method->signature.calling_convention & 0x20u) == 0u)
            {
                return method;
            }
        }

        return NULL;
    }
}

struct zaclr_result zaclr_native_System_RuntimeHelpers::ReflectionInvocation_RunClassConstructor___STATIC__VOID__VALUETYPE(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    const struct zaclr_type_desc* type_desc = NULL;
    const struct zaclr_method_desc* cctor = NULL;

    /* The managed QCall stub passes QCallTypeHandle, which is a valuetype wrapper
       around a RuntimeType reference.  Our earlier path assumed arg0 was already
       an object handle.  Accept both shapes:
       1) direct object reference
       2) valuetype payload whose first pointer-sized slot contains the RuntimeType */
    struct zaclr_result result = zaclr_native_call_frame_arg_object(&frame, 0u, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        struct zaclr_stack_value* arg0 = zaclr_native_call_frame_arg(&frame, 0u);
        console_write("[ZACLR][runtimehelpers] RunClassConstructor arg_object status=");
        console_write_dec((uint64_t)result.status);
        console_write(" category=");
        console_write_dec((uint64_t)result.category);
        if (arg0 != NULL)
        {
            console_write(" kind=");
            console_write_dec((uint64_t)arg0->kind);
            console_write(" flags=");
            console_write_hex64((uint64_t)arg0->flags);
            console_write(" payload=");
            console_write_dec((uint64_t)arg0->payload_size);
            console_write(" type_token=");
            console_write_hex64((uint64_t)arg0->type_token_raw);
            console_write(" raw=");
            console_write_hex64((uint64_t)arg0->data.raw);
        }
        console_write("\n");

        if (arg0 == NULL)
        {
            return result;
        }

        if (arg0->kind == ZACLR_STACK_VALUE_VALUETYPE)
        {
            const void* payload = zaclr_stack_value_payload_const(arg0);
            if (payload != NULL)
            {
                /* CoreCLR QCallTypeHandle layout:
                   field0 = void* _ptr
                   field1 = native int _handle

                   ZACLR's current CoreLib native convention already treats the
                   RuntimeType native handle as a casted zaclr_object_handle, see:
                   - RuntimeTypeHandle::ToIntPtr
                   - System.Type::GetTypeFromHandleUnsafe

                   So use the same convention here instead of introducing a second
                   incompatible translation model. */
                uintptr_t native_type_handle = 0u;
                if (arg0->payload_size >= sizeof(void*) + sizeof(uintptr_t))
                {
                    kernel_memcpy(&native_type_handle,
                                  (const uint8_t*)payload + sizeof(void*),
                                  sizeof(native_type_handle));
                }

                console_write("[ZACLR][runtimehelpers] valuetype payload native_handle=");
                console_write_hex64((uint64_t)native_type_handle);
                console_write(" current_cached_handle=");
                console_write_hex64((uint64_t)runtime_type_handle);
                console_write("\n");

                runtime_type_handle = (zaclr_object_handle)native_type_handle;
            }
        }
        else if (arg0->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE)
        {
            runtime_type_handle = zaclr_heap_get_object_handle(&frame.runtime->heap, arg0->data.object_reference);
        }
        else if (arg0->kind == ZACLR_STACK_VALUE_I8)
        {
            /* Fallback: raw native pointer value flowing through a wrapper path */
            struct zaclr_object_desc* runtime_type_object = (struct zaclr_object_desc*)(uintptr_t)arg0->data.i8;
            runtime_type_handle = runtime_type_object != NULL
                ? zaclr_heap_get_object_handle(&frame.runtime->heap, runtime_type_object)
                : 0u;
        }

        if (runtime_type_handle == 0u)
        {
            console_write("[ZACLR][runtimehelpers] RunClassConstructor fallback decode failed\n");
            return result;
        }
    }

    console_write("[ZACLR][runtimehelpers] RunClassConstructor handle=");
    console_write_hex64((uint64_t)runtime_type_handle);
    console_write("\n");

    if (runtime_type_handle == 0u)
    {
        console_write("[ZACLR][runtimehelpers] zero runtime_type_handle\n");
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    runtime_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, runtime_type_handle);
    if (runtime_type != NULL && runtime_type->type_assembly != NULL)
    {
        console_write("[ZACLR][runtimehelpers] runtime_type token=");
        console_write_hex64((uint64_t)runtime_type->type_token.raw);
        console_write(" asm=");
        console_write_hex64((uint64_t)(uintptr_t)runtime_type->type_assembly);
        console_write("\n");

        type_desc = zaclr_type_map_find_by_token(&runtime_type->type_assembly->type_map, runtime_type->type_token);
        if (type_desc == NULL)
        {
            console_write("[ZACLR][runtimehelpers] type_desc lookup failed token=");
            console_write_hex64((uint64_t)runtime_type->type_token.raw);
            console_write("\n");
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
        }
    }
    else
    {
        console_write("[ZACLR][runtimehelpers] runtime_type lookup failed, no valid translation path yet\n");
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    console_write("[ZACLR][runtimehelpers] type_desc=");
    console_write(type_desc->type_name.text != NULL ? type_desc->type_name.text : "<null>");
    console_write(" id=");
    console_write_dec((uint64_t)type_desc->id);
    console_write("\n");

    if (runtime_type->type_assembly->type_initializer_state != NULL && type_desc->id != 0u && type_desc->id <= runtime_type->type_assembly->type_map.count)
    {
        uint8_t* state = &((uint8_t*)runtime_type->type_assembly->type_initializer_state)[type_desc->id - 1u];
        if (*state == 2u || *state == 1u)
        {
            return zaclr_native_call_frame_set_void(&frame);
        }

        cctor = find_type_initializer_method(runtime_type->type_assembly, type_desc);
        if (cctor == NULL)
        {
            *state = 2u;
            return zaclr_native_call_frame_set_void(&frame);
        }

        *state = 1u;
        result = zaclr_engine_execute_method(&frame.runtime->engine,
                                             frame.runtime,
                                             &frame.runtime->boot_launch,
                                             runtime_type->type_assembly,
                                             cctor);
        if (result.status != ZACLR_STATUS_OK)
        {
            *state = 0u;
            return result;
        }

        *state = 2u;
        return zaclr_native_call_frame_set_void(&frame);
    }

    return zaclr_native_call_frame_set_void(&frame);
}
