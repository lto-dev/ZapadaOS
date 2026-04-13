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

    /* Search the runtime type's vtable for the override by name.
       The vtable was built by build_vtable() which uses name-matching for
       overrides, so the slot at runtime_mt contains the most-derived
       implementation for each virtual method name. */
    for (slot_index = 0u; slot_index < runtime_mt->vtable_slot_count; ++slot_index)
    {
        const struct zaclr_method_desc* slot = runtime_mt->vtable[slot_index];
        if (slot != NULL
            && slot->name.text != NULL
            && declared_method->name.text != NULL
            && text_equals(slot->name.text, declared_method->name.text))
        {
            return slot;
        }
    }

    /* No vtable match found - return the declared method unchanged */
    return declared_method;
}
