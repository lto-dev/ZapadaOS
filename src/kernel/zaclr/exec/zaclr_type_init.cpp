#include <kernel/zaclr/exec/zaclr_type_init.h>

#include <kernel/zaclr/exec/zaclr_engine.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

namespace
{
    static constexpr uint16_t k_method_attribute_static = 0x0010u;
    static constexpr uint8_t k_type_initializer_state_running = 1u;
    static constexpr uint8_t k_type_initializer_state_complete = 2u;
}

extern "C" const struct zaclr_method_desc* zaclr_find_type_initializer(const struct zaclr_loaded_assembly* assembly,
                                                                       const struct zaclr_type_desc* type)
{
    uint32_t method_index;

    if (assembly == NULL || type == NULL || assembly->method_map.methods == NULL)
    {
        return NULL;
    }

    for (method_index = 0u; method_index < type->method_count; ++method_index)
    {
        const struct zaclr_method_desc* method = &assembly->method_map.methods[type->first_method_index + method_index];
        if (method->name.text != NULL
            && zaclr_text_equals(method->name.text, ".cctor")
            && method->signature.parameter_count == 0u
            && (method->signature.calling_convention & 0x20u) == 0u)
        {
            return method;
        }
    }

    return NULL;
}

extern "C" struct zaclr_result zaclr_ensure_type_initializer_ran(struct zaclr_runtime* runtime,
                                                                  const struct zaclr_loaded_assembly* assembly,
                                                                  const struct zaclr_type_desc* type)
{
    uint32_t type_index;
    uint8_t* state;
    const struct zaclr_method_desc* cctor;
    struct zaclr_result result;

    if (runtime == NULL || assembly == NULL || type == NULL || type->id == 0u)
    {
        return zaclr_result_ok();
    }

    if (assembly->type_initializer_state == NULL || type->id > assembly->type_map.count)
    {
        return zaclr_result_ok();
    }

    type_index = type->id - 1u;
    state = &assembly->type_initializer_state[type_index];
    if (*state == k_type_initializer_state_complete || *state == k_type_initializer_state_running)
    {
        return zaclr_result_ok();
    }

    cctor = zaclr_find_type_initializer(assembly, type);
    if (cctor == NULL)
    {
        *state = k_type_initializer_state_complete;
        return zaclr_result_ok();
    }

    *state = k_type_initializer_state_running;
    result = zaclr_engine_execute_method(&runtime->engine,
                                         runtime,
                                         &runtime->boot_launch,
                                         assembly,
                                         cctor);
    if (result.status != ZACLR_STATUS_OK)
    {
        *state = 0u;
        return result;
    }

    *state = k_type_initializer_state_complete;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_ensure_type_initializer_ran_with_context(
    struct zaclr_runtime* runtime,
    const struct zaclr_loaded_assembly* assembly,
    const struct zaclr_type_desc* type,
    const struct zaclr_generic_context* type_context)
{
    uint32_t type_index;
    uint8_t* state;
    const struct zaclr_method_desc* cctor;
    struct zaclr_result result;

    if (runtime == NULL || assembly == NULL || type == NULL || type->id == 0u)
    {
        return zaclr_result_ok();
    }

    if (assembly->type_initializer_state == NULL || type->id > assembly->type_map.count)
    {
        return zaclr_result_ok();
    }

    type_index = type->id - 1u;
    state = &assembly->type_initializer_state[type_index];
    if (*state == k_type_initializer_state_complete || *state == k_type_initializer_state_running)
    {
        return zaclr_result_ok();
    }

    cctor = zaclr_find_type_initializer(assembly, type);
    if (cctor == NULL)
    {
        *state = k_type_initializer_state_complete;
        return zaclr_result_ok();
    }

    *state = k_type_initializer_state_running;
    result = zaclr_engine_execute_method_with_type_context(&runtime->engine,
                                                           runtime,
                                                           &runtime->boot_launch,
                                                           assembly,
                                                           cctor,
                                                           type_context);
    if (result.status != ZACLR_STATUS_OK)
    {
        *state = 0u;
        return result;
    }

    *state = k_type_initializer_state_complete;
    return zaclr_result_ok();
}

extern "C" bool zaclr_method_is_type_initializer_trigger(const struct zaclr_method_desc* method)
{
    if (method == NULL || method->name.text == NULL)
    {
        return false;
    }

    if (zaclr_text_equals(method->name.text, ".cctor"))
    {
        return false;
    }

    return (method->method_flags & k_method_attribute_static) != 0u;
}
