#include <kernel/zaclr/heap/zaclr_object.h>

#include <kernel/support/kernel_memory.h>

#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/loader/zaclr_assembly_registry.h>
#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>
#include <kernel/zaclr/metadata/zaclr_token.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_field_layout.h>
#include <kernel/zaclr/typesystem/zaclr_type_prepare.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static constexpr uint16_t k_field_flag_static = 0x0010u;

    static size_t zaclr_reference_object_allocation_size(const struct zaclr_method_table* method_table,
                                                         uint32_t compatibility_field_capacity)
    {
        if (method_table != NULL && method_table->instance_size > ZACLR_OBJECT_HEADER_SIZE)
        {
            return sizeof(struct zaclr_reference_object_desc)
                 + (size_t)(method_table->instance_size - ZACLR_OBJECT_HEADER_SIZE);
        }

        return sizeof(struct zaclr_reference_object_desc)
             + (sizeof(struct zaclr_stack_value) * (size_t)compatibility_field_capacity);
    }

    static uint8_t* reference_data(struct zaclr_reference_object_desc* object)
    {
        return object != NULL ? ((uint8_t*)object + sizeof(struct zaclr_reference_object_desc)) : NULL;
    }

    static bool compatibility_slot_for_type(const struct zaclr_loaded_assembly* assembly,
                                            const struct zaclr_type_desc* type_desc,
                                            uint32_t target_field_row,
                                            uint32_t* io_slot)
    {
        const struct zaclr_type_desc* base_type = NULL;

        if (assembly == NULL || type_desc == NULL || io_slot == NULL)
        {
            return false;
        }

        if (zaclr_token_matches_table(&type_desc->extends, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            base_type = zaclr_type_map_find_by_token(&assembly->type_map, type_desc->extends);
            if (base_type != NULL && compatibility_slot_for_type(assembly, base_type, target_field_row, io_slot))
            {
                return true;
            }
        }

        for (uint32_t field_index = 0u; field_index < type_desc->field_count; ++field_index)
        {
            uint32_t field_row = type_desc->field_list + field_index;
            struct zaclr_field_row row = {};
            if (zaclr_metadata_reader_get_field_row(&assembly->metadata, field_row, &row).status != ZACLR_STATUS_OK)
            {
                continue;
            }

            if ((row.flags & k_field_flag_static) != 0u)
            {
                continue;
            }

            if (field_row == target_field_row)
            {
                return true;
            }

            ++(*io_slot);
        }

        return false;
    }

    static bool compatibility_slot_for_method_table(const struct zaclr_method_table* method_table,
                                                    uint32_t target_field_row,
                                                    uint32_t* io_slot)
    {
        const struct zaclr_loaded_assembly* assembly;
        const struct zaclr_type_desc* type_desc;

        if (method_table == NULL || io_slot == NULL)
        {
            return false;
        }

        if (method_table->parent != NULL && compatibility_slot_for_method_table(method_table->parent, target_field_row, io_slot))
        {
            return true;
        }

        assembly = method_table->assembly;
        type_desc = method_table->type_desc;
        if (assembly == NULL || type_desc == NULL)
        {
            return false;
        }

        for (uint32_t field_index = 0u; field_index < type_desc->field_count; ++field_index)
        {
            uint32_t field_row = type_desc->field_list + field_index;
            struct zaclr_field_row row = {};
            if (zaclr_metadata_reader_get_field_row(&assembly->metadata, field_row, &row).status != ZACLR_STATUS_OK)
            {
                continue;
            }

            if ((row.flags & k_field_flag_static) != 0u)
            {
                continue;
            }

            if (field_row == target_field_row)
            {
                return true;
            }

            ++(*io_slot);
        }

        return false;
    }

    static bool compatibility_slot_for_token(const struct zaclr_reference_object_desc* object,
                                             struct zaclr_token token,
                                             uint32_t* out_slot)
    {
        const struct zaclr_loaded_assembly* assembly;
        const struct zaclr_type_desc* type_desc;
        const struct zaclr_method_table* method_table;
        uint32_t slot = 0u;

        if (object == NULL || out_slot == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
        {
            return false;
        }

        method_table = object->object.header.method_table;
        if (method_table != NULL && method_table->assembly != NULL && method_table->type_desc != NULL)
        {
            if (compatibility_slot_for_method_table(method_table, zaclr_token_row(&token), &slot))
            {
                *out_slot = slot;
                return true;
            }

            assembly = method_table->assembly;
            type_desc = method_table->type_desc;
        }
        else
        {
            assembly = object->object.owning_assembly;
            if (assembly == NULL || object->object.type_id == 0u)
            {
                return false;
            }

            type_desc = zaclr_type_map_find_by_token(&assembly->type_map,
                                                     zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_TYPEDEF << 24) | object->object.type_id));
            if (type_desc == NULL)
            {
                return false;
            }
        }

        if (!compatibility_slot_for_type(assembly, type_desc, zaclr_token_row(&token), &slot))
        {
            return false;
        }

        *out_slot = slot;
        return true;
    }

    static const uint8_t* reference_data_const(const struct zaclr_reference_object_desc* object)
    {
        return object != NULL ? ((const uint8_t*)object + sizeof(struct zaclr_reference_object_desc)) : NULL;
    }

    static const struct zaclr_field_layout* find_field_layout(const struct zaclr_object_desc* object,
                                                              struct zaclr_token token)
    {
        const struct zaclr_method_table* method_table;
        uint32_t index;

        if (object == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
        {
            return NULL;
        }

        method_table = object->header.method_table;

        /* Walk the method table parent chain to find inherited fields.
           In .NET, ldfld uses field tokens owned by any parent type in the
           hierarchy.  For example, RuntimeType inherits fields from Type,
           TypeInfo, MemberInfo, etc.  We must search up the chain. */
        while (method_table != NULL)
        {
            if (method_table->instance_fields != NULL)
            {
                for (index = 0u; index < method_table->instance_field_count; ++index)
                {
                    const struct zaclr_field_layout* layout = &method_table->instance_fields[index];
                    if (layout->is_static == 0u && layout->field_token_row == zaclr_token_row(&token))
                    {
                        return layout;
                    }
                }
            }

            method_table = method_table->parent;
        }

        return NULL;
    }

    static uint8_t* instance_field_base(struct zaclr_object_desc* object)
    {
        if (object == NULL)
        {
            return NULL;
        }

        switch (zaclr_object_family(object))
        {
            case ZACLR_OBJECT_FAMILY_INSTANCE:
                return reference_data((struct zaclr_reference_object_desc*)object);

            case ZACLR_OBJECT_FAMILY_STRING:
                return ((uint8_t*)object) + sizeof(struct zaclr_object_desc);

            case ZACLR_OBJECT_FAMILY_RUNTIME_TYPE:
                return ((uint8_t*)object) + sizeof(struct zaclr_runtime_type_desc);

            default:
                return NULL;
        }
    }

    static const uint8_t* instance_field_base_const(const struct zaclr_object_desc* object)
    {
        return instance_field_base((struct zaclr_object_desc*)object);
    }

    static void* object_instance_field_address(struct zaclr_object_desc* object,
                                               struct zaclr_token token)
    {
        const struct zaclr_field_layout* layout = find_field_layout(object, token);
        uint8_t* base = instance_field_base(object);
        return (layout != NULL && base != NULL) ? (void*)(base + layout->byte_offset) : NULL;
    }

    static const void* object_instance_field_address_const(const struct zaclr_object_desc* object,
                                                           struct zaclr_token token)
    {
        const struct zaclr_field_layout* layout = find_field_layout(object, token);
        const uint8_t* base = instance_field_base_const(object);
        return (layout != NULL && base != NULL) ? (const void*)(base + layout->byte_offset) : NULL;
    }

    static struct zaclr_result store_field_value(void* address,
                                                 const struct zaclr_field_layout* layout,
                                                 const struct zaclr_stack_value* value)
    {
        uint64_t wide = 0u;

        if (address == NULL || layout == NULL || value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
        }

        if (layout->element_type == ZACLR_ELEMENT_TYPE_VALUETYPE)
        {
            switch (layout->field_size)
            {
                case 1u:
                case 2u:
                case 4u:
                case 8u:
                    if (value->kind != ZACLR_STACK_VALUE_I4 && value->kind != ZACLR_STACK_VALUE_I8)
                    {
                        console_write("[ZACLR][object] valuetype store unsupported kind=");
                        console_write_dec((uint64_t)value->kind);
                        console_write(" field_size=");
                        console_write_dec((uint64_t)layout->field_size);
                        console_write(" element_type=");
                        console_write_dec((uint64_t)layout->element_type);
                        console_write("\n");
                        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_HEAP);
                    }
                    break;
                default:
                    console_write("[ZACLR][object] valuetype store unsupported size=");
                    console_write_dec((uint64_t)layout->field_size);
                    console_write(" element_type=");
                    console_write_dec((uint64_t)layout->element_type);
                    console_write("\n");
                    return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_HEAP);
            }
        }

        if (layout->is_reference != 0u)
        {
            struct zaclr_object_desc* reference = value->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE ? value->data.object_reference : NULL;
            zaclr_gc_write_barrier((struct zaclr_object_desc**)address, reference);
            return zaclr_result_ok();
        }

        if (value->kind == ZACLR_STACK_VALUE_I8)
        {
            wide = (uint64_t)value->data.i8;
        }
        else if (value->kind == ZACLR_STACK_VALUE_I4)
        {
            wide = (uint64_t)(uint32_t)value->data.i4;
        }
        else
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_HEAP);
        }

        switch (layout->field_size)
        {
            case 1u:
            {
                uint8_t v = (uint8_t)wide;
                kernel_memcpy(address, &v, sizeof(v));
                return zaclr_result_ok();
            }
            case 2u:
            {
                uint16_t v = (uint16_t)wide;
                kernel_memcpy(address, &v, sizeof(v));
                return zaclr_result_ok();
            }
            case 4u:
            {
                uint32_t v = (uint32_t)wide;
                kernel_memcpy(address, &v, sizeof(v));
                return zaclr_result_ok();
            }
            case 8u:
                kernel_memcpy(address, &wide, sizeof(wide));
                return zaclr_result_ok();
            default:
                return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_HEAP);
        }
    }

    static struct zaclr_result load_field_value(const void* address,
                                                const struct zaclr_field_layout* layout,
                                                struct zaclr_stack_value* out_value)
    {
        if (address == NULL || layout == NULL || out_value == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
        }

        *out_value = {};
        if (layout->element_type == ZACLR_ELEMENT_TYPE_VALUETYPE)
        {
            return zaclr_stack_value_set_valuetype(out_value,
                                                   layout->nested_type_token_raw,
                                                   address,
                                                   layout->field_size);
        }

        if (layout->is_reference != 0u)
        {
            struct zaclr_object_desc* reference = NULL;
            kernel_memcpy(&reference, address, sizeof(reference));
            out_value->kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
            out_value->data.object_reference = reference;
            return zaclr_result_ok();
        }

        if (layout->field_size <= 4u)
        {
            uint32_t raw = 0u;
            kernel_memcpy(&raw, address, layout->field_size);
            out_value->kind = ZACLR_STACK_VALUE_I4;
            out_value->data.i4 = (int32_t)raw;
            return zaclr_result_ok();
        }

        if (layout->field_size == 8u)
        {
            uint64_t raw = 0u;
            kernel_memcpy(&raw, address, sizeof(raw));
            out_value->kind = ZACLR_STACK_VALUE_I8;
            out_value->data.i8 = (int64_t)raw;
            return zaclr_result_ok();
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_HEAP);
    }

    static struct zaclr_result boxed_value_prepare_layout(struct zaclr_runtime* runtime,
                                                          const struct zaclr_boxed_value_desc* boxed_value,
                                                          struct zaclr_token token,
                                                          const struct zaclr_loaded_assembly** out_assembly,
                                                          struct zaclr_method_table** out_method_table,
                                                          const struct zaclr_field_layout** out_layout)
    {
        const struct zaclr_type_desc* type_desc = NULL;
        struct zaclr_token value_type_token;
        uint32_t index;
        struct zaclr_result result;

        if (runtime == NULL || boxed_value == NULL || out_assembly == NULL || out_method_table == NULL || out_layout == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
        }

        *out_assembly = NULL;
        *out_method_table = NULL;
        *out_layout = NULL;
        value_type_token = zaclr_token_make(boxed_value->type_token_raw);
        if (!zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
        }

        result = zaclr_type_system_resolve_type_desc(zaclr_object_owning_assembly(&boxed_value->object),
                                                     runtime,
                                                     value_type_token,
                                                     out_assembly,
                                                     &type_desc);
        if (result.status != ZACLR_STATUS_OK)
        {
            console_write("[ZACLR][boxed] resolve type failed token=");
            console_write_hex64((uint64_t)value_type_token.raw);
            console_write(" status=");
            console_write_hex64((uint64_t)result.status);
            console_write(" category=");
            console_write_hex64((uint64_t)result.category);
            console_write("\n");
            return result;
        }

        if (*out_assembly == NULL || type_desc == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
        }

        result = zaclr_type_prepare(runtime,
                                    (struct zaclr_loaded_assembly*)*out_assembly,
                                    type_desc,
                                    out_method_table);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (*out_method_table == NULL || (*out_method_table)->instance_fields == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
        }

        for (index = 0u; index < (*out_method_table)->instance_field_count; ++index)
        {
            const struct zaclr_field_layout* layout = &(*out_method_table)->instance_fields[index];
            if (layout->is_static == 0u && layout->field_token_row == zaclr_token_row(&token))
            {
                *out_layout = layout;
                return zaclr_result_ok();
            }
        }

        console_write("[ZACLR][boxed] layout miss boxed_type_token=");
        console_write_hex64((uint64_t)boxed_value->type_token_raw);
        console_write(" field_token=");
        console_write_hex64((uint64_t)token.raw);
        console_write(" field_count=");
        console_write_dec((uint64_t)(*out_method_table)->instance_field_count);
        console_write(" first_field=");
        console_write_hex64((uint64_t)((*out_method_table)->instance_field_count != 0u ? (*out_method_table)->instance_fields[0].field_token_row : 0u));
        console_write("\n");
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    static struct zaclr_result resolve_named_instance_field_token(const struct zaclr_loaded_assembly* assembly,
                                                                  const struct zaclr_type_desc* type_desc,
                                                                  const char* field_name,
                                                                  struct zaclr_token* out_token)
    {
        uint32_t field_index;
        struct zaclr_result result;

        if (assembly == NULL || type_desc == NULL || field_name == NULL || out_token == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
        }

        *out_token = zaclr_token_make(0u);
        if (zaclr_token_matches_table(&type_desc->extends, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            const struct zaclr_type_desc* base_type = zaclr_type_map_find_by_token(&assembly->type_map, type_desc->extends);
            if (base_type != NULL)
            {
                result = resolve_named_instance_field_token(assembly, base_type, field_name, out_token);
                if (result.status == ZACLR_STATUS_OK)
                {
                    return result;
                }
            }
        }

        for (field_index = 0u; field_index < type_desc->field_count; ++field_index)
        {
            uint32_t field_row = type_desc->field_list + field_index;
            struct zaclr_field_row row = {};
            struct zaclr_name_view resolved_name = {};
            result = zaclr_metadata_reader_get_field_row(&assembly->metadata, field_row, &row);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            result = zaclr_metadata_reader_get_string(&assembly->metadata, row.name_index, &resolved_name);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if ((row.flags & k_field_flag_static) == 0u
                && resolved_name.text != NULL
                && zaclr_text_equals(resolved_name.text, field_name))
            {
                *out_token = zaclr_token_make(((uint32_t)ZACLR_TOKEN_TABLE_FIELD << 24) | field_row);
                return zaclr_result_ok();
            }
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    static struct zaclr_result initialize_runtime_type_managed_handle(struct zaclr_runtime* runtime,
                                                                      struct zaclr_runtime_type_desc* runtime_type)
    {
        struct zaclr_token handle_field_token;
        struct zaclr_stack_value handle_value = {};
        const struct zaclr_method_table* method_table;
        struct zaclr_result result;

        if (runtime == NULL || runtime_type == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
        }

        method_table = runtime_type->object.header.method_table;
        if (method_table == NULL || method_table->assembly == NULL || method_table->type_desc == NULL)
        {
            return zaclr_result_ok();
        }

        result = resolve_named_instance_field_token(method_table->assembly,
                                                    method_table->type_desc,
                                                    "m_handle",
                                                    &handle_field_token);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        handle_value.kind = ZACLR_STACK_VALUE_I8;
        handle_value.data.i8 = (int64_t)runtime_type->native_type_handle;
        return zaclr_object_store_field(runtime,
                                        &runtime_type->object,
                                        handle_field_token,
                                        &handle_value);
    }
}

extern "C" uint32_t zaclr_object_flags(const struct zaclr_object_desc* object)
{
    return object != NULL ? object->header.flags : 0u;
}

extern "C" uint32_t zaclr_object_size_bytes(const struct zaclr_object_desc* object)
{
    return object != NULL ? object->size_bytes : 0u;
}

extern "C" uint32_t zaclr_object_family(const struct zaclr_object_desc* object)
{
    return object != NULL ? object->family : (uint32_t)ZACLR_OBJECT_FAMILY_UNKNOWN;
}

extern "C" uint32_t zaclr_object_contains_references(const struct zaclr_object_desc* object)
{
    return object != NULL && (object->header.flags & ZACLR_OBJECT_FLAG_CONTAINS_REFERENCES) != 0u;
}

extern "C" uint32_t zaclr_object_is_marked(const struct zaclr_object_desc* object)
{
    return object != NULL && object->gc_mark != 0u;
}

extern "C" void zaclr_object_set_marked(struct zaclr_object_desc* object, uint32_t marked)
{
    if (object != NULL)
    {
        object->gc_mark = marked != 0u ? 1u : 0u;
    }
}

extern "C" struct zaclr_method_table* zaclr_object_method_table(struct zaclr_object_desc* object)
{
    return object != NULL ? object->header.method_table : NULL;
}

extern "C" const struct zaclr_method_table* zaclr_object_method_table_const(const struct zaclr_object_desc* object)
{
    return object != NULL ? object->header.method_table : NULL;
}

extern "C" const struct zaclr_loaded_assembly* zaclr_object_owning_assembly(const struct zaclr_object_desc* object)
{
    if (object == NULL)
    {
        return NULL;
    }

    if (object->header.method_table != NULL && object->header.method_table->assembly != NULL)
    {
        return object->header.method_table->assembly;
    }

    return object->owning_assembly;
}

extern "C" zaclr_type_id zaclr_object_type_id(const struct zaclr_object_desc* object)
{
    if (object == NULL)
    {
        return 0u;
    }

    if (object->header.method_table != NULL
        && object->header.method_table->type_desc != NULL
        && zaclr_token_matches_table(&object->header.method_table->type_desc->token, ZACLR_TOKEN_TABLE_TYPEDEF))
    {
        return zaclr_token_row(&object->header.method_table->type_desc->token);
    }

    return object->type_id;
}

extern "C" struct zaclr_result zaclr_boxed_value_allocate(struct zaclr_heap* heap,
                                                             struct zaclr_token token,
                                                             const struct zaclr_stack_value* value,
                                                             struct zaclr_boxed_value_desc** out_value)
{
    struct zaclr_boxed_value_desc* boxed_value;
    struct zaclr_result result;

    if (heap == NULL || value == NULL || out_value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    *out_value = NULL;
    result = zaclr_heap_allocate_object(heap,
                                        sizeof(struct zaclr_boxed_value_desc),
                                        NULL,
                                        0u,
                                        ZACLR_OBJECT_FAMILY_INSTANCE,
                                        ZACLR_OBJECT_FLAG_BOXED_VALUE,
                                        (struct zaclr_object_desc**)&boxed_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    boxed_value->type_token_raw = token.raw;
    boxed_value->reserved = 0u;
    boxed_value->value = *value;
    *out_value = boxed_value;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_boxed_value_allocate_handle(struct zaclr_heap* heap,
                                                                    struct zaclr_token token,
                                                                    const struct zaclr_stack_value* value,
                                                                    zaclr_object_handle* out_handle)
{
    struct zaclr_boxed_value_desc* boxed_value;
    struct zaclr_result result;

    if (out_handle == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    *out_handle = 0u;
    result = zaclr_boxed_value_allocate(heap, token, value, &boxed_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    *out_handle = zaclr_heap_get_object_handle(heap, &boxed_value->object);
    return zaclr_result_ok();
}

extern "C" struct zaclr_boxed_value_desc* zaclr_boxed_value_from_handle(struct zaclr_heap* heap,
                                                                          zaclr_object_handle handle)
{
    return (struct zaclr_boxed_value_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" const struct zaclr_boxed_value_desc* zaclr_boxed_value_from_handle_const(const struct zaclr_heap* heap,
                                                                                      zaclr_object_handle handle)
{
    return (const struct zaclr_boxed_value_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" struct zaclr_result zaclr_reference_object_allocate(struct zaclr_heap* heap,
                                                                  const struct zaclr_loaded_assembly* owning_assembly,
                                                                  zaclr_type_id type_id,
                                                                  struct zaclr_token type_token,
                                                                  uint32_t field_capacity,
                                                                  struct zaclr_reference_object_desc** out_object)
{
    struct zaclr_reference_object_desc* object;
    struct zaclr_result result;
    struct zaclr_method_table* method_table = NULL;
    const struct zaclr_type_desc* type_desc = NULL;
    uint32_t compatibility_field_capacity;

    if (heap == NULL || out_object == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    if (heap->runtime != NULL && owning_assembly != NULL && zaclr_token_matches_table(&type_token, ZACLR_TOKEN_TABLE_TYPEDEF))
    {
        type_desc = zaclr_type_map_find_by_token(&owning_assembly->type_map, type_token);
        if (type_desc != NULL)
        {
            result = zaclr_type_prepare(heap->runtime,
                                        (struct zaclr_loaded_assembly*)owning_assembly,
                                        type_desc,
                                        &method_table);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }
        }
    }

    *out_object = NULL;
    compatibility_field_capacity = (method_table != NULL && zaclr_method_table_is_prepared(method_table) != 0u)
        ? 0u
        : field_capacity;

    result = zaclr_heap_allocate_object(heap,
                                        zaclr_reference_object_allocation_size(method_table, compatibility_field_capacity),
                                        owning_assembly,
                                        type_id,
                                        ZACLR_OBJECT_FAMILY_INSTANCE,
                                        ZACLR_OBJECT_FLAG_REFERENCE_TYPE
                                            | ((method_table != NULL && zaclr_method_table_contains_references(method_table) != 0u)
                                                   ? (uint32_t)ZACLR_OBJECT_FLAG_CONTAINS_REFERENCES
                                                   : 0u),
                                        (struct zaclr_object_desc**)&object);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    object->object.header.method_table = method_table;
    object->compatibility_field_capacity = compatibility_field_capacity;
    object->reserved = 0u;
    *out_object = object;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_reference_object_allocate_handle(struct zaclr_heap* heap,
                                                                        const struct zaclr_loaded_assembly* owning_assembly,
                                                                        zaclr_type_id type_id,
                                                                        struct zaclr_token type_token,
                                                                        uint32_t field_capacity,
                                                                        zaclr_object_handle* out_handle)
{
    struct zaclr_reference_object_desc* object;
    struct zaclr_result result;

    if (out_handle == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    *out_handle = 0u;
    result = zaclr_reference_object_allocate(heap, owning_assembly, type_id, type_token, field_capacity, &object);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    *out_handle = zaclr_heap_get_object_handle(heap, &object->object);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_runtime_type_allocate(struct zaclr_heap* heap,
                                                             const struct zaclr_loaded_assembly* type_assembly,
                                                             struct zaclr_token type_token,
                                                             struct zaclr_runtime_type_desc** out_runtime_type)
{
    struct zaclr_runtime_type_desc* runtime_type;
    struct zaclr_result result;
    struct zaclr_method_table* method_table = NULL;
    const struct zaclr_type_desc* runtime_type_desc = NULL;
    const struct zaclr_loaded_assembly* corelib = NULL;
    size_t allocation_size = sizeof(struct zaclr_runtime_type_desc);

    if (heap == NULL || out_runtime_type == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    if (heap->runtime != NULL)
    {
        const struct zaclr_app_domain* domain = zaclr_runtime_current_domain(heap->runtime);
        if (domain != NULL)
        {
            corelib = zaclr_assembly_registry_find_by_name(&domain->registry, "System.Private.CoreLib");
        }
        if (corelib != NULL)
        {
            struct zaclr_member_name_ref type_name = {"System", "RuntimeType", NULL};
            runtime_type_desc = zaclr_type_system_find_type_by_name(corelib, &type_name);
            if (runtime_type_desc != NULL)
            {
                result = zaclr_type_prepare(heap->runtime,
                                            (struct zaclr_loaded_assembly*)corelib,
                                            runtime_type_desc,
                                            &method_table);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
            }
        }
    }

    /*
     * The object's CLR identity is RuntimeType (defined in CoreLib), not the
     * type this RuntimeType instance represents.  owning_assembly must be
     * CoreLib and type_id must be RuntimeType's typedef row so that
     * castclass / isinst walk the correct inheritance chain
     * (RuntimeType -> Type -> MemberInfo -> Object).
     *
     * type_assembly / type_token are stored on zaclr_runtime_type_desc to
     * track what this RuntimeType represents -- they are NOT the object's
     * CLR class identity.
     */
    const struct zaclr_loaded_assembly* object_assembly = (corelib != NULL) ? corelib : type_assembly;
    zaclr_type_id runtime_type_id = (runtime_type_desc != NULL)
        ? (zaclr_type_id)zaclr_token_row(&runtime_type_desc->token)
        : 0u;

    if (method_table != NULL && method_table->instance_size > ZACLR_OBJECT_HEADER_SIZE)
    {
        allocation_size += (size_t)(method_table->instance_size - ZACLR_OBJECT_HEADER_SIZE);
    }

    *out_runtime_type = NULL;
    result = zaclr_heap_allocate_object(heap,
                                        allocation_size,
                                        object_assembly,
                                        runtime_type_id,
                                        ZACLR_OBJECT_FAMILY_RUNTIME_TYPE,
                                        ZACLR_OBJECT_FLAG_REFERENCE_TYPE,
                                        (struct zaclr_object_desc**)&runtime_type);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    runtime_type->object.header.method_table = method_table;
    runtime_type->type_assembly = type_assembly;
    runtime_type->type_token = type_token;
    runtime_type->native_type_handle = (uintptr_t)&runtime_type->object;
    if (heap->runtime != NULL)
    {
        result = initialize_runtime_type_managed_handle(heap->runtime, runtime_type);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
    }

    *out_runtime_type = runtime_type;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_runtime_type_allocate_handle(struct zaclr_heap* heap,
                                                                    const struct zaclr_loaded_assembly* type_assembly,
                                                                    struct zaclr_token type_token,
                                                                    zaclr_object_handle* out_handle)
{
    struct zaclr_runtime_type_desc* runtime_type;
    struct zaclr_result result;

    if (out_handle == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    *out_handle = 0u;
    result = zaclr_runtime_type_allocate(heap, type_assembly, type_token, &runtime_type);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    *out_handle = zaclr_heap_get_object_handle(heap, &runtime_type->object);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_runtime_assembly_get_or_create(struct zaclr_heap* heap,
                                                                     struct zaclr_loaded_assembly* assembly,
                                                                     zaclr_object_handle* out_handle)
{
    struct zaclr_result result;
    struct zaclr_reference_object_desc* object;
    struct zaclr_method_table* method_table = NULL;
    const struct zaclr_type_desc* runtime_assembly_desc = NULL;
    const struct zaclr_loaded_assembly* corelib = NULL;
    zaclr_type_id assembly_type_id = 0u;

    if (heap == NULL || assembly == NULL || out_handle == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    /* Return cached handle if already created */
    if (assembly->exposed_assembly_handle != 0u)
    {
        *out_handle = assembly->exposed_assembly_handle;
        return zaclr_result_ok();
    }

    /* Find System.Reflection.RuntimeAssembly type in CoreLib */
    if (heap->runtime != NULL)
    {
        const struct zaclr_app_domain* domain = zaclr_runtime_current_domain(heap->runtime);
        if (domain != NULL)
        {
            corelib = zaclr_assembly_registry_find_by_name(&domain->registry, "System.Private.CoreLib");
        }
        if (corelib != NULL)
        {
            struct zaclr_member_name_ref type_name = {"System.Reflection", "RuntimeAssembly", NULL};
            runtime_assembly_desc = zaclr_type_system_find_type_by_name(corelib, &type_name);
            if (runtime_assembly_desc != NULL)
            {
                assembly_type_id = (zaclr_type_id)zaclr_token_row(&runtime_assembly_desc->token);
                result = zaclr_type_prepare(heap->runtime,
                                            (struct zaclr_loaded_assembly*)corelib,
                                            runtime_assembly_desc,
                                            &method_table);
                if (result.status != ZACLR_STATUS_OK)
                {
                    return result;
                }
            }
        }
    }

    /* Allocate the RuntimeAssembly reference object.
       owning_assembly = corelib (the assembly where RuntimeAssembly is defined)
       type_id = RuntimeAssembly typedef row in corelib */
    const struct zaclr_loaded_assembly* object_assembly = (corelib != NULL) ? corelib : assembly;
    result = zaclr_heap_allocate_object(heap,
                                        sizeof(struct zaclr_reference_object_desc)
                                            + (method_table != NULL && method_table->instance_size > ZACLR_OBJECT_HEADER_SIZE
                                                ? (size_t)(method_table->instance_size - ZACLR_OBJECT_HEADER_SIZE)
                                                : sizeof(struct zaclr_stack_value) * 4u),
                                        object_assembly,
                                        assembly_type_id,
                                        ZACLR_OBJECT_FAMILY_INSTANCE,
                                        ZACLR_OBJECT_FLAG_REFERENCE_TYPE,
                                        (struct zaclr_object_desc**)&object);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    object->object.header.method_table = method_table;
    object->compatibility_field_capacity = (method_table != NULL) ? 0u : 4u;
    object->reserved = 0u;

    /* Set m_assembly field (IntPtr) to point back to the native assembly.
       m_assembly is the 5th field after the event+3 ref fields:
       _ModuleResolve (event, slot 0)
       m_fullname     (string, slot 1)
       m_syncRoot     (object, slot 2)
       m_assembly     (IntPtr, slot 3)
       In CoreCLR this is at a fixed layout offset; we set it via the
       method table field layout if available, or compatibility slot 3. */
    if (method_table != NULL && zaclr_method_table_is_prepared(method_table) != 0u)
    {
        /* Use the proper field layout through the method table */
        uint8_t* data = (uint8_t*)object + sizeof(struct zaclr_reference_object_desc);
        /* m_assembly is the field named "m_assembly" in RuntimeAssembly.
           Find its offset from the method table field layout. */
        if (method_table->instance_size > ZACLR_OBJECT_HEADER_SIZE)
        {
            /* Walk the field layout to find "m_assembly" offset.
               For now, store the pointer at the known offset for IntPtr field.
               CoreCLR layout: _ModuleResolve(8) + m_fullname(8) + m_syncRoot(8) + m_assembly(8) = offset 24 from data start */
            if (method_table->instance_size - ZACLR_OBJECT_HEADER_SIZE >= 32u)
            {
                *(uintptr_t*)(data + 24u) = (uintptr_t)assembly;
            }
        }
    }
    else
    {
        /* Compatibility path: store in slot 3 */
        struct zaclr_stack_value* fields = (struct zaclr_stack_value*)((uint8_t*)object + sizeof(struct zaclr_reference_object_desc));
        fields[3].kind = ZACLR_STACK_VALUE_I8;
        fields[3].data.i8 = (int64_t)(uintptr_t)assembly;
    }

    *out_handle = zaclr_heap_get_object_handle(heap, &object->object);
    assembly->exposed_assembly_handle = *out_handle;
    return zaclr_result_ok();
}

extern "C" struct zaclr_reference_object_desc* zaclr_reference_object_from_handle(struct zaclr_heap* heap,
                                                                                    zaclr_object_handle handle)
{
    return (struct zaclr_reference_object_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" struct zaclr_runtime_type_desc* zaclr_runtime_type_from_handle(struct zaclr_heap* heap,
                                                                           zaclr_object_handle handle)
{
    return (struct zaclr_runtime_type_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" const struct zaclr_reference_object_desc* zaclr_reference_object_from_handle_const(const struct zaclr_heap* heap,
                                                                                                zaclr_object_handle handle)
{
    return (const struct zaclr_reference_object_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" const struct zaclr_runtime_type_desc* zaclr_runtime_type_from_handle_const(const struct zaclr_heap* heap,
                                                                                        zaclr_object_handle handle)
{
    return (const struct zaclr_runtime_type_desc*)zaclr_heap_get_object(heap, handle);
}

extern "C" struct zaclr_result zaclr_runtime_type_find_by_native_handle(struct zaclr_runtime* runtime,
                                                                         uintptr_t native_handle,
                                                                         zaclr_object_handle* out_handle)
{
    const struct zaclr_app_domain* domain;

    if (runtime == NULL || out_handle == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    *out_handle = 0u;
    console_write("[ZACLR][rtype-lookup] search native_handle=");
    console_write_hex64((uint64_t)native_handle);
    console_write("\n");
    if (native_handle == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    domain = zaclr_runtime_current_domain(runtime);
    if (domain == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    for (uint32_t asm_index = 0u; asm_index < domain->registry.count; ++asm_index)
    {
        const struct zaclr_loaded_assembly* assembly = &domain->registry.entries[asm_index];
        if (assembly == NULL || assembly->runtime_type_cache == NULL)
        {
            continue;
        }

        for (uint32_t type_index = 0u; type_index < assembly->runtime_type_cache_count; ++type_index)
        {
            zaclr_object_handle candidate_handle = assembly->runtime_type_cache[type_index];
            const struct zaclr_runtime_type_desc* candidate;
            uintptr_t candidate_native_handle;

            if (candidate_handle == 0u)
            {
                continue;
            }

            candidate = zaclr_runtime_type_from_handle_const(&runtime->heap, candidate_handle);
            if (candidate == NULL)
            {
                continue;
            }

            candidate_native_handle = candidate->native_type_handle;
            if (type_index < 4u)
            {
                console_write("[ZACLR][rtype-lookup] candidate idx=");
                console_write_dec((uint64_t)type_index);
                console_write(" handle=");
                console_write_hex64((uint64_t)candidate_handle);
                console_write(" native=");
                console_write_hex64((uint64_t)candidate_native_handle);
                console_write(" token=");
                console_write_hex64((uint64_t)candidate->type_token.raw);
                console_write("\n");
            }
            if (candidate_native_handle == native_handle)
            {
                *out_handle = candidate_handle;
                return zaclr_result_ok();
            }
        }
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
}

extern "C" const struct zaclr_field_layout* zaclr_reference_object_field_layout(const struct zaclr_reference_object_desc* object,
                                                                                  struct zaclr_token token)
{
    return find_field_layout(&object->object, token);
}

extern "C" void* zaclr_reference_object_field_address(struct zaclr_reference_object_desc* object,
                                                       struct zaclr_token token)
{
    return object_instance_field_address(&object->object, token);
}

extern "C" const void* zaclr_reference_object_field_address_const(const struct zaclr_reference_object_desc* object,
                                                                   struct zaclr_token token)
{
    return object_instance_field_address_const(&object->object, token);
}

extern "C" struct zaclr_result zaclr_reference_object_store_field(struct zaclr_reference_object_desc* object,
                                                                   struct zaclr_token token,
                                                                   const struct zaclr_stack_value* value)
{
    const struct zaclr_field_layout* layout;
    void* address;

    if (object == NULL || value == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    layout = find_field_layout(&object->object, token);
    if (layout != NULL)
    {
        address = zaclr_reference_object_field_address(object, token);
        return store_field_value(address, layout, value);
    }

    if (object->compatibility_field_capacity != 0u)
    {
        uint32_t slot = 0u;
        struct zaclr_stack_value* fields = (struct zaclr_stack_value*)reference_data(object);
        if (compatibility_slot_for_token(object, token, &slot) && slot < object->compatibility_field_capacity)
        {
            return zaclr_stack_value_assign(&fields[slot], value);
        }
    }

    console_write("[ZACLR][object] store field miss token_row=");
    console_write_dec((uint64_t)zaclr_token_row(&token));
    console_write(" type_id=");
    console_write_dec((uint64_t)object->object.type_id);
    console_write(" method_table=");
    console_write_hex64((uint64_t)(uintptr_t)object->object.header.method_table);
    console_write(" mt_field_list=");
    console_write_dec((uint64_t)(object->object.header.method_table != NULL && object->object.header.method_table->type_desc != NULL
        ? object->object.header.method_table->type_desc->field_list
        : 0u));
    console_write(" mt_field_count=");
    console_write_dec((uint64_t)(object->object.header.method_table != NULL && object->object.header.method_table->type_desc != NULL
        ? object->object.header.method_table->type_desc->field_count
        : 0u));
    console_write(" instance_field_count=");
    console_write_dec((uint64_t)(object->object.header.method_table != NULL ? object->object.header.method_table->instance_field_count : 0u));
    console_write(" compat_capacity=");
    console_write_dec((uint64_t)object->compatibility_field_capacity);
    console_write("\n");

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
}

extern "C" struct zaclr_result zaclr_reference_object_load_field(const struct zaclr_reference_object_desc* object,
                                                                  struct zaclr_token token,
                                                                  struct zaclr_stack_value* out_value)
{
    const struct zaclr_field_layout* layout;
    const void* address;

    if (object == NULL || out_value == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    layout = find_field_layout(&object->object, token);
    if (layout != NULL)
    {
        address = zaclr_reference_object_field_address_const(object, token);
        return load_field_value(address, layout, out_value);
    }

    if (object->compatibility_field_capacity != 0u)
    {
        uint32_t slot = 0u;
        const struct zaclr_stack_value* fields = (const struct zaclr_stack_value*)reference_data_const(object);
        if (compatibility_slot_for_token(object, token, &slot) && slot < object->compatibility_field_capacity)
        {
            *out_value = fields[slot];
            return zaclr_result_ok();
        }
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
}

extern "C" struct zaclr_stack_value* zaclr_reference_object_field_storage(struct zaclr_reference_object_desc* object,
                                                                            struct zaclr_token token)
{
    return (struct zaclr_stack_value*)zaclr_reference_object_field_address(object, token);
}

extern "C" const struct zaclr_stack_value* zaclr_reference_object_field_storage_const(const struct zaclr_reference_object_desc* object,
                                                                                         struct zaclr_token token)
{
    return (const struct zaclr_stack_value*)zaclr_reference_object_field_address_const(object, token);
}

extern "C" struct zaclr_result zaclr_boxed_value_store_field(struct zaclr_runtime* runtime,
                                                               struct zaclr_boxed_value_desc* boxed_value,
                                                               struct zaclr_token token,
                                                               const struct zaclr_stack_value* value)
{
    const struct zaclr_loaded_assembly* type_assembly = NULL;
    struct zaclr_method_table* method_table = NULL;
    const struct zaclr_field_layout* layout = NULL;
    uint8_t* value_bytes;
    void* field_address;

    if (runtime == NULL || boxed_value == NULL || value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    if (boxed_value_prepare_layout(runtime, boxed_value, token, &type_assembly, &method_table, &layout).status != ZACLR_STATUS_OK)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    value_bytes = (uint8_t*)zaclr_stack_value_payload(&boxed_value->value);
    if (value_bytes == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    field_address = value_bytes + layout->byte_offset;
    return store_field_value(field_address, layout, value);
}

extern "C" struct zaclr_result zaclr_object_store_field(struct zaclr_runtime* runtime,
                                                           struct zaclr_object_desc* object,
                                                           struct zaclr_token token,
                                                           const struct zaclr_stack_value* value)
{
    if (runtime == NULL || value == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    if (object == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    if ((zaclr_object_flags(object) & ZACLR_OBJECT_FLAG_BOXED_VALUE) != 0u)
    {
        return zaclr_boxed_value_store_field(runtime, (struct zaclr_boxed_value_desc*)object, token, value);
    }

    if ((zaclr_object_flags(object) & ZACLR_OBJECT_FLAG_REFERENCE_TYPE) != 0u)
    {
        if (zaclr_object_family(object) == ZACLR_OBJECT_FAMILY_STRING
            || zaclr_object_family(object) == ZACLR_OBJECT_FAMILY_RUNTIME_TYPE)
        {
            const struct zaclr_field_layout* layout = find_field_layout(object, token);
            void* address = object_instance_field_address(object, token);
            if (layout != NULL && address != NULL)
            {
                return store_field_value(address, layout, value);
            }

            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
        }

        return zaclr_reference_object_store_field((struct zaclr_reference_object_desc*)object, token, value);
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_HEAP);
}

extern "C" struct zaclr_result zaclr_object_store_field_handle(struct zaclr_runtime* runtime,
                                                                 zaclr_object_handle handle,
                                                                 struct zaclr_token token,
                                                                 const struct zaclr_stack_value* value)
{
    if (runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    return zaclr_object_store_field(runtime, zaclr_heap_get_object(&runtime->heap, handle), token, value);
}

extern "C" struct zaclr_result zaclr_boxed_value_load_field(struct zaclr_runtime* runtime,
                                                              const struct zaclr_boxed_value_desc* boxed_value,
                                                              struct zaclr_token token,
                                                              struct zaclr_stack_value* out_value)
{
    const struct zaclr_loaded_assembly* type_assembly = NULL;
    struct zaclr_method_table* method_table = NULL;
    const struct zaclr_field_layout* layout = NULL;
    const uint8_t* value_bytes;
    const void* field_address;

    if (runtime == NULL || boxed_value == NULL || out_value == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    if (boxed_value_prepare_layout(runtime, boxed_value, token, &type_assembly, &method_table, &layout).status != ZACLR_STATUS_OK)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    value_bytes = (const uint8_t*)zaclr_stack_value_payload_const(&boxed_value->value);
    if (value_bytes == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    field_address = value_bytes + layout->byte_offset;
    return load_field_value(field_address, layout, out_value);
}

extern "C" struct zaclr_result zaclr_object_load_field(struct zaclr_runtime* runtime,
                                                         const struct zaclr_object_desc* object,
                                                         struct zaclr_token token,
                                                         struct zaclr_stack_value* out_value)
{
    if (runtime == NULL || out_value == NULL || !zaclr_token_matches_table(&token, ZACLR_TOKEN_TABLE_FIELD))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    if (object == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
    }

    if ((zaclr_object_flags(object) & ZACLR_OBJECT_FLAG_BOXED_VALUE) != 0u)
    {
        return zaclr_boxed_value_load_field(runtime, (const struct zaclr_boxed_value_desc*)object, token, out_value);
    }

    if ((zaclr_object_flags(object) & ZACLR_OBJECT_FLAG_REFERENCE_TYPE) != 0u)
    {
        if (zaclr_object_family(object) == ZACLR_OBJECT_FAMILY_STRING
            || zaclr_object_family(object) == ZACLR_OBJECT_FAMILY_RUNTIME_TYPE)
        {
            const struct zaclr_field_layout* layout = find_field_layout(object, token);
            const void* address = object_instance_field_address_const(object, token);
            if (layout != NULL && address != NULL)
            {
                return load_field_value(address, layout, out_value);
            }

            if (zaclr_object_family(object) == ZACLR_OBJECT_FAMILY_RUNTIME_TYPE)
            {
                /* RuntimeTypeHandle.m_type field access pattern:
                   In .NET, GetTypeFromHandle(RuntimeTypeHandle handle) does
                   ldfld RuntimeTypeHandle::m_type to extract the Type from the
                   handle struct.  In ZACLR, ldtoken materializes a RuntimeType
                   object directly (not wrapped in a RuntimeTypeHandle value type).
                   When ldfld on a RUNTIME_TYPE object fails to find the field in
                   the RuntimeType hierarchy, it means the IL is accessing the
                   RuntimeTypeHandle.m_type wrapper field.  The correct result is
                   the RuntimeType object itself since it IS the m_type value. */
                *out_value = {};
                out_value->kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
                out_value->data.object_reference = (struct zaclr_object_desc*)object;
                return zaclr_result_ok();
            }

            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_HEAP);
        }

        {
            struct zaclr_result result = zaclr_reference_object_load_field((const struct zaclr_reference_object_desc*)object, token, out_value);
            if (result.status == ZACLR_STATUS_OK)
            {
                return result;
            }

            if (object->header.method_table != NULL
                && object->header.method_table->type_desc != NULL
                && object->header.method_table->type_desc->type_namespace.text != NULL
                && object->header.method_table->type_desc->type_name.text != NULL
                && object->header.method_table->type_desc->type_namespace.text[0] == 'S'
                && object->header.method_table->type_desc->type_namespace.text[1] == 'y'
                && object->header.method_table->type_desc->type_namespace.text[2] == 's'
                && object->header.method_table->type_desc->type_namespace.text[3] == 't'
                && object->header.method_table->type_desc->type_namespace.text[4] == 'e'
                && object->header.method_table->type_desc->type_namespace.text[5] == 'm'
                && object->header.method_table->type_desc->type_namespace.text[6] == '.'
                && object->header.method_table->type_desc->type_namespace.text[7] == 'R'
                && object->header.method_table->type_desc->type_namespace.text[8] == 'e'
                && object->header.method_table->type_desc->type_namespace.text[9] == 'f'
                && object->header.method_table->type_desc->type_namespace.text[10] == 'l'
                && object->header.method_table->type_desc->type_namespace.text[11] == 'e'
                && object->header.method_table->type_desc->type_namespace.text[12] == 'c'
                && object->header.method_table->type_desc->type_namespace.text[13] == 't'
                && object->header.method_table->type_desc->type_namespace.text[14] == 'i'
                && object->header.method_table->type_desc->type_namespace.text[15] == 'o'
                && object->header.method_table->type_desc->type_namespace.text[16] == 'n'
                && object->header.method_table->type_desc->type_namespace.text[17] == '\0'
                && object->header.method_table->type_desc->type_name.text[0] == 'R'
                && object->header.method_table->type_desc->type_name.text[1] == 'u'
                && object->header.method_table->type_desc->type_name.text[2] == 'n'
                && object->header.method_table->type_desc->type_name.text[3] == 't'
                && object->header.method_table->type_desc->type_name.text[4] == 'i'
                && object->header.method_table->type_desc->type_name.text[5] == 'm'
                && object->header.method_table->type_desc->type_name.text[6] == 'e'
                && object->header.method_table->type_desc->type_name.text[7] == 'A'
                && object->header.method_table->type_desc->type_name.text[8] == 's'
                && object->header.method_table->type_desc->type_name.text[9] == 's'
                && object->header.method_table->type_desc->type_name.text[10] == 'e'
                && object->header.method_table->type_desc->type_name.text[11] == 'm'
                && object->header.method_table->type_desc->type_name.text[12] == 'b'
                && object->header.method_table->type_desc->type_name.text[13] == 'l'
                && object->header.method_table->type_desc->type_name.text[14] == 'y'
                && object->header.method_table->type_desc->type_name.text[15] == '\0')
            {
                *out_value = {};
                out_value->kind = ZACLR_STACK_VALUE_OBJECT_REFERENCE;
                out_value->data.object_reference = (struct zaclr_object_desc*)object;
                return zaclr_result_ok();
            }

            return result;
        }
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_IMPLEMENTED, ZACLR_STATUS_CATEGORY_HEAP);
}

extern "C" struct zaclr_result zaclr_object_load_field_handle(struct zaclr_runtime* runtime,
                                                                zaclr_object_handle handle,
                                                                struct zaclr_token token,
                                                                struct zaclr_stack_value* out_value)
{
    if (runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_HEAP);
    }

    return zaclr_object_load_field(runtime, zaclr_heap_get_object(&runtime->heap, handle), token, out_value);
}

extern "C" uint32_t zaclr_stack_value_contains_references(const struct zaclr_stack_value* value)
{
    if (value == NULL)
    {
        return 0u;
    }

    return value->kind == ZACLR_STACK_VALUE_OBJECT_REFERENCE && value->data.object_reference != NULL;
}

extern "C" void zaclr_gc_write_barrier(struct zaclr_object_desc** slot,
                                         struct zaclr_object_desc* value)
{
    if (slot != NULL)
    {
        *slot = value;
    }
}
