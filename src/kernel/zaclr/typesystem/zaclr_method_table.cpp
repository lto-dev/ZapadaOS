#include <kernel/zaclr/typesystem/zaclr_method_table.h>
#include <kernel/zaclr/metadata/zaclr_method_map.h>

namespace
{
    static bool text_equals(const char* a, const char* b)
    {
        if (a == NULL || b == NULL) return false;
        while (*a != '\0' && *b != '\0')
        {
            if (*a != *b) return false;
            ++a;
            ++b;
        }
        return *a == *b;
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

    static bool virtual_method_name_matches(const char* slot_name,
                                            const char* declared_name)
    {
        uint32_t slot_length;
        uint32_t declared_length;
        uint32_t suffix_start;

        if (slot_name == NULL || declared_name == NULL)
        {
            return false;
        }

        if (text_equals(slot_name, declared_name))
        {
            return true;
        }

        slot_length = text_length(slot_name);
        declared_length = text_length(declared_name);
        if (slot_length <= declared_length + 1u)
        {
            return false;
        }

        suffix_start = slot_length - declared_length;
        if (slot_name[suffix_start - 1u] != '.')
        {
            return false;
        }

        return text_equals(slot_name + suffix_start, declared_name);
    }

    static bool virtual_slot_matches_declared_method(const struct zaclr_method_desc* slot,
                                                     const struct zaclr_method_desc* declared_method)
    {
        if (slot == NULL
            || declared_method == NULL
            || slot->name.text == NULL
            || declared_method->name.text == NULL
            || !virtual_method_name_matches(slot->name.text, declared_method->name.text))
        {
            return false;
        }

        return slot->signature.parameter_count == declared_method->signature.parameter_count
            && slot->signature.generic_parameter_count == declared_method->signature.generic_parameter_count
            && ((slot->signature.calling_convention & 0x20u) == (declared_method->signature.calling_convention & 0x20u));
    }
}

extern "C" uint8_t zaclr_method_table_has_finalizer(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return (mt->flags & ZACLR_MT_FLAG_HAS_FINALIZER) != 0u ? 1u : 0u;
}

extern "C" uint8_t zaclr_method_table_is_value_type(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return (mt->flags & ZACLR_MT_FLAG_IS_VALUE_TYPE) != 0u ? 1u : 0u;
}

extern "C" uint8_t zaclr_method_table_is_interface(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return (mt->flags & ZACLR_MT_FLAG_IS_INTERFACE) != 0u ? 1u : 0u;
}

extern "C" uint8_t zaclr_method_table_contains_references(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return (mt->flags & ZACLR_MT_FLAG_CONTAINS_REFERENCES) != 0u ? 1u : 0u;
}

extern "C" uint32_t zaclr_method_table_instance_size(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return mt->instance_size;
}

extern "C" uint32_t zaclr_method_table_component_size(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    if ((mt->flags & ZACLR_MT_FLAG_HAS_COMPONENT_SIZE) == 0u)
    {
        return 0u;
    }

    return mt->component_size;
}

extern "C" uint8_t zaclr_method_table_is_enum(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return (mt->flags & ZACLR_MT_FLAG_IS_ENUM) != 0u ? 1u : 0u;
}

extern "C" uint8_t zaclr_method_table_is_abstract(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return (mt->flags & ZACLR_MT_FLAG_IS_ABSTRACT) != 0u ? 1u : 0u;
}

extern "C" uint8_t zaclr_method_table_is_sealed(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return (mt->flags & ZACLR_MT_FLAG_IS_SEALED) != 0u ? 1u : 0u;
}

extern "C" uint8_t zaclr_method_table_has_cctor(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return (mt->flags & ZACLR_MT_FLAG_HAS_CCTOR) != 0u ? 1u : 0u;
}

extern "C" uint8_t zaclr_method_table_is_string(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return (mt->flags & ZACLR_MT_FLAG_IS_STRING) != 0u ? 1u : 0u;
}

extern "C" uint8_t zaclr_method_table_is_array(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return (mt->flags & ZACLR_MT_FLAG_IS_ARRAY) != 0u ? 1u : 0u;
}

extern "C" uint8_t zaclr_method_table_is_delegate(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return (mt->flags & ZACLR_MT_FLAG_IS_DELEGATE) != 0u ? 1u : 0u;
}

extern "C" uint8_t zaclr_method_table_is_prepared(const struct zaclr_method_table* mt)
{
    if (mt == NULL)
    {
        return 0u;
    }

    return mt->preparation_state == ZACLR_MT_PREP_COMPLETE ? 1u : 0u;
}

extern "C" const struct zaclr_method_desc* zaclr_method_table_resolve_virtual(
    const struct zaclr_method_table* runtime_mt,
    const struct zaclr_method_desc* declared_method)
{
    uint32_t slot_index;

    if (declared_method == NULL)
    {
        return NULL;
    }

    /* Non-virtual methods are not overridden */
    if ((declared_method->method_flags & 0x0040u /* METHOD_FLAG_VIRTUAL */) == 0u)
    {
        return declared_method;
    }

    if (runtime_mt == NULL || runtime_mt->vtable == NULL || runtime_mt->vtable_slot_count == 0u)
    {
        return declared_method;
    }

    /* Search the runtime type's vtable for the override by name and signature shape. */
    for (slot_index = 0u; slot_index < runtime_mt->vtable_slot_count; ++slot_index)
    {
        const struct zaclr_method_desc* slot = runtime_mt->vtable[slot_index];
        if (virtual_slot_matches_declared_method(slot, declared_method))
        {
            return slot;
        }
    }

    /* No vtable match found - return the declared method unchanged */
    return declared_method;
}
