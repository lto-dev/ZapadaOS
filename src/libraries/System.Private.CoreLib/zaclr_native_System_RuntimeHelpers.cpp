#include "zaclr_native_System_RuntimeHelpers.h"

#include <kernel/zaclr/exec/zaclr_engine.h>
#include <kernel/zaclr/metadata/zaclr_type_map.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

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
    const struct zaclr_runtime_type_desc* runtime_type;
    const struct zaclr_type_desc* type_desc;
    const struct zaclr_method_desc* cctor;

    struct zaclr_result result = zaclr_native_call_frame_arg_object(&frame, 0u, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (runtime_type_handle == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    runtime_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, runtime_type_handle);
    if (runtime_type == NULL || runtime_type->type_assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    type_desc = zaclr_type_map_find_by_token(&runtime_type->type_assembly->type_map, runtime_type->type_token);
    if (type_desc == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }

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
