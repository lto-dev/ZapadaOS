#include <kernel/zaclr/typesystem/zaclr_type_prepare.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>

#include <kernel/support/kernel_memory.h>

extern "C" {
#include <kernel/console.h>
}

/* ECMA-335 TypeDef flags (II.23.1.15) */
#define TYPEDEF_FLAG_INTERFACE         0x00000020u
#define TYPEDEF_FLAG_ABSTRACT          0x00000080u
#define TYPEDEF_FLAG_SEALED            0x00000100u
#define TYPEDEF_FLAG_SERIALIZABLE      0x00002000u

/* ECMA-335 FieldDef flags (II.23.1.5) */
#define FIELD_FLAG_STATIC              0x0010u
#define FIELD_FLAG_ACCESS_MASK         0x0007u

/* ECMA-335 MethodDef flags (II.23.1.10) */
#define METHOD_FLAG_STATIC             0x0010u
#define METHOD_FLAG_VIRTUAL            0x0040u
#define METHOD_FLAG_NEW_SLOT           0x0100u
#define METHOD_FLAG_ABSTRACT           0x0400u
#define METHOD_FLAG_SPECIAL_NAME       0x0800u
#define METHOD_FLAG_RT_SPECIAL_NAME    0x1000u

/* Signature calling convention flags */
#define SIG_CONV_HASTHIS               0x20u

namespace
{
    static bool text_equals(const char* left, const char* right)
    {
        size_t index = 0u;

        if (left == NULL || right == NULL)
        {
            return left == right;
        }

        while (left[index] != '\0' || right[index] != '\0')
        {
            if (left[index] != right[index])
            {
                return false;
            }

            ++index;
        }

        return true;
    }

    static bool extends_well_known_type(struct zaclr_runtime* /* runtime */,
                                        const struct zaclr_loaded_assembly* assembly,
                                        const struct zaclr_type_desc* type_desc,
                                        const char* target_namespace,
                                        const char* target_name)
    {
        struct zaclr_token extends_token = type_desc->extends;

        if (zaclr_token_is_nil(&extends_token))
        {
            return false;
        }

        if (zaclr_token_matches_table(&extends_token, ZACLR_TOKEN_TABLE_TYPEDEF))
        {
            const struct zaclr_type_desc* parent = zaclr_type_map_find_by_token(&assembly->type_map, extends_token);
            if (parent == NULL)
            {
                return false;
            }

            return text_equals(parent->type_namespace.text, target_namespace)
                && text_equals(parent->type_name.text, target_name);
        }

        if (zaclr_token_matches_table(&extends_token, ZACLR_TOKEN_TABLE_TYPEREF))
        {
            struct zaclr_member_name_ref name = {};
            struct zaclr_result result = zaclr_metadata_get_typeref_name(&assembly->metadata, zaclr_token_row(&extends_token), &name);
            if (result.status != ZACLR_STATUS_OK)
            {
                return false;
            }

            return text_equals(name.type_namespace, target_namespace)
                && text_equals(name.type_name, target_name);
        }

        return false;
    }

    static struct zaclr_result resolve_parent_method_table(struct zaclr_runtime* runtime,
                                                           struct zaclr_loaded_assembly* assembly,
                                                           const struct zaclr_type_desc* type_desc,
                                                           struct zaclr_method_table** out_parent)
    {
        struct zaclr_token extends_token = type_desc->extends;
        const struct zaclr_loaded_assembly* parent_assembly = NULL;
        const struct zaclr_type_desc* parent_type = NULL;
        struct zaclr_result result;

        *out_parent = NULL;

        if (zaclr_token_is_nil(&extends_token))
        {
            return zaclr_result_ok();
        }

        result = zaclr_type_system_resolve_type_desc(assembly, runtime, extends_token,
                                                     &parent_assembly, &parent_type);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (parent_type == NULL || parent_assembly == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
        }

        /* Recursively prepare the parent type.
           Cast away const on parent_assembly since we need to populate its cache. */
        return zaclr_type_prepare(runtime,
                                  (struct zaclr_loaded_assembly*)parent_assembly,
                                  parent_type,
                                  out_parent);
    }

    static uint32_t compute_type_flags(struct zaclr_runtime* runtime,
                                       const struct zaclr_loaded_assembly* assembly,
                                       const struct zaclr_type_desc* type_desc,
                                       const struct zaclr_method_table* /* parent */)
    {
        uint32_t flags = ZACLR_MT_FLAG_NONE;
        uint32_t typedef_flags = type_desc->flags;
        uint32_t method_index;

        /* Interface */
        if ((typedef_flags & TYPEDEF_FLAG_INTERFACE) != 0u)
        {
            flags |= ZACLR_MT_FLAG_IS_INTERFACE;
        }

        /* Abstract */
        if ((typedef_flags & TYPEDEF_FLAG_ABSTRACT) != 0u)
        {
            flags |= ZACLR_MT_FLAG_IS_ABSTRACT;
        }

        /* Sealed */
        if ((typedef_flags & TYPEDEF_FLAG_SEALED) != 0u)
        {
            flags |= ZACLR_MT_FLAG_IS_SEALED;
        }

        /* Value type: extends System.ValueType (but not System.Enum) */
        if (extends_well_known_type(runtime, assembly, type_desc, "System", "ValueType"))
        {
            flags |= ZACLR_MT_FLAG_IS_VALUE_TYPE;
        }

        /* Enum: extends System.Enum */
        if (extends_well_known_type(runtime, assembly, type_desc, "System", "Enum"))
        {
            flags |= ZACLR_MT_FLAG_IS_VALUE_TYPE;
            flags |= ZACLR_MT_FLAG_IS_ENUM;
        }

        /* Delegate: extends System.MulticastDelegate or System.Delegate */
        if (extends_well_known_type(runtime, assembly, type_desc, "System", "MulticastDelegate")
            || extends_well_known_type(runtime, assembly, type_desc, "System", "Delegate"))
        {
            flags |= ZACLR_MT_FLAG_IS_DELEGATE;
        }

        /* String: check if this IS System.String */
        if (type_desc->type_namespace.text != NULL
            && type_desc->type_name.text != NULL
            && text_equals(type_desc->type_namespace.text, "System")
            && text_equals(type_desc->type_name.text, "String"))
        {
            flags |= ZACLR_MT_FLAG_IS_STRING;
            flags |= ZACLR_MT_FLAG_HAS_COMPONENT_SIZE;
        }

        /* Scan methods for Finalize and .cctor */
        for (method_index = 0u; method_index < type_desc->method_count; ++method_index)
        {
            uint32_t absolute_index = type_desc->first_method_index + method_index;
            if (absolute_index >= assembly->method_map.count)
            {
                break;
            }

            const struct zaclr_method_desc* method = &assembly->method_map.methods[absolute_index];

            /* Has finalizer: method named "Finalize" with hasthis and 0 params */
            if (method->name.text != NULL
                && text_equals(method->name.text, "Finalize")
                && (method->signature.calling_convention & SIG_CONV_HASTHIS) != 0u
                && method->signature.parameter_count == 0u)
            {
                flags |= ZACLR_MT_FLAG_HAS_FINALIZER;
            }

            /* Has .cctor: static method named ".cctor" */
            if (method->name.text != NULL
                && text_equals(method->name.text, ".cctor")
                && (method->method_flags & METHOD_FLAG_STATIC) != 0u)
            {
                flags |= ZACLR_MT_FLAG_HAS_CCTOR;
            }
        }

        return flags;
    }

    static uint32_t align_to(uint32_t value, uint32_t alignment)
    {
        if (alignment == 0u || alignment == 1u)
        {
            return value;
        }

        return (value + alignment - 1u) & ~(alignment - 1u);
    }

    struct field_layout_result {
        struct zaclr_field_layout* instance_fields;
        uint32_t instance_field_count;
        struct zaclr_field_layout* static_fields_layout;
        uint32_t static_field_count;
        uint32_t own_instance_bytes;
        uint32_t gc_reference_count;
        uint32_t alignment_requirement;
    };

    static struct zaclr_result compute_field_layouts(struct zaclr_runtime* runtime,
                                                     const struct zaclr_loaded_assembly* assembly,
                                                     const struct zaclr_type_desc* type_desc,
                                                     uint32_t parent_instance_bytes,
                                                     struct field_layout_result* out)
    {
        uint32_t field_index;
        uint32_t instance_count = 0u;
        uint32_t static_count = 0u;
        uint32_t current_offset;
        struct zaclr_field_layout* instance_fields = NULL;
        struct zaclr_field_layout* static_fields_out = NULL;

        out->instance_fields = NULL;
        out->instance_field_count = 0u;
        out->static_fields_layout = NULL;
        out->static_field_count = 0u;
        out->own_instance_bytes = 0u;
        out->gc_reference_count = 0u;
        out->alignment_requirement = 1u;

        if (type_desc->field_count == 0u)
        {
            return zaclr_result_ok();
        }

        /* First pass: count instance vs static fields */
        for (field_index = 0u; field_index < type_desc->field_count; ++field_index)
        {
            uint32_t field_row = type_desc->field_list + field_index;
            struct zaclr_field_row row = {};
            struct zaclr_result result = zaclr_metadata_reader_get_field_row(&assembly->metadata, field_row, &row);
            if (result.status != ZACLR_STATUS_OK)
            {
                return result;
            }

            if ((row.flags & FIELD_FLAG_STATIC) != 0u)
            {
                ++static_count;
            }
            else
            {
                ++instance_count;
            }
        }

        /* Allocate instance field layouts */
        if (instance_count != 0u)
        {
            instance_fields = (struct zaclr_field_layout*)kernel_alloc(sizeof(struct zaclr_field_layout) * instance_count);
            if (instance_fields == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_METADATA);
            }

            kernel_memset(instance_fields, 0, sizeof(struct zaclr_field_layout) * instance_count);
        }

        /* Allocate static field layouts */
        if (static_count != 0u)
        {
            static_fields_out = (struct zaclr_field_layout*)kernel_alloc(sizeof(struct zaclr_field_layout) * static_count);
            if (static_fields_out == NULL)
            {
                if (instance_fields != NULL)
                {
                    kernel_free(instance_fields);
                }

                return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_METADATA);
            }

            kernel_memset(static_fields_out, 0, sizeof(struct zaclr_field_layout) * static_count);
        }

        /* Second pass: compute layout for each field */
        uint32_t inst_idx = 0u;
        uint32_t stat_idx = 0u;
        current_offset = parent_instance_bytes;

        for (field_index = 0u; field_index < type_desc->field_count; ++field_index)
        {
            uint32_t field_row = type_desc->field_list + field_index;
            struct zaclr_field_row row = {};
            struct zaclr_slice sig_blob = {};
            struct zaclr_signature_type sig_type = {};
            struct zaclr_result result;
            uint8_t element_type;
            uint8_t is_ref;
            uint32_t field_size;
            uint32_t alignment;
            struct zaclr_method_table* nested_method_table = NULL;

            result = zaclr_metadata_reader_get_field_row(&assembly->metadata, field_row, &row);
            if (result.status != ZACLR_STATUS_OK)
            {
                goto cleanup_error;
            }

            result = zaclr_metadata_reader_get_blob(&assembly->metadata, row.signature_blob_index, &sig_blob);
            if (result.status != ZACLR_STATUS_OK)
            {
                goto cleanup_error;
            }

            result = zaclr_field_layout_parse_field_signature(&sig_blob, &sig_type);
            if (result.status != ZACLR_STATUS_OK)
            {
                goto cleanup_error;
            }

            element_type = sig_type.element_type;
            is_ref = zaclr_field_layout_is_reference(element_type);
            if (element_type == ZACLR_ELEMENT_TYPE_VALUETYPE)
            {
                const struct zaclr_loaded_assembly* nested_assembly = NULL;
                const struct zaclr_type_desc* nested_type = NULL;

                if (!zaclr_token_is_nil(&sig_type.type_token))
                {
                    result = zaclr_type_system_resolve_type_desc(assembly,
                                                                 runtime,
                                                                 sig_type.type_token,
                                                                 &nested_assembly,
                                                                 &nested_type);
                    if (result.status != ZACLR_STATUS_OK)
                    {
                        goto cleanup_error;
                    }
                }

                if (nested_assembly == NULL || nested_type == NULL)
                {
                    result = zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_METADATA);
                    goto cleanup_error;
                }

                result = zaclr_type_prepare(runtime,
                                            (struct zaclr_loaded_assembly*)nested_assembly,
                                            nested_type,
                                            &nested_method_table);
                if (result.status != ZACLR_STATUS_OK)
                {
                    goto cleanup_error;
                }

                if (nested_method_table == NULL || nested_method_table->instance_size < ZACLR_OBJECT_HEADER_SIZE)
                {
                    result = zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_METADATA);
                    goto cleanup_error;
                }

                field_size = nested_method_table->instance_size - ZACLR_OBJECT_HEADER_SIZE;
                alignment = nested_method_table->field_alignment_requirement != 0u
                    ? nested_method_table->field_alignment_requirement
                    : 1u;
            }
            else
            {
                field_size = zaclr_field_layout_size_from_element_type(element_type);
                if (field_size == 0u)
                {
                    field_size = 8u; /* fallback for unknown types */
                }

                alignment = zaclr_field_layout_compute_alignment(element_type);
            }

            if (alignment == 0u)
            {
                alignment = 1u;
            }

            if (out->alignment_requirement < alignment)
            {
                out->alignment_requirement = alignment;
            }

            if ((row.flags & FIELD_FLAG_STATIC) != 0u)
            {
                /* Static field */
                if (stat_idx < static_count)
                {
                    static_fields_out[stat_idx].field_token_row = field_row;
                    static_fields_out[stat_idx].byte_offset = 0u; /* no instance offset for statics */
                    static_fields_out[stat_idx].nested_type_token_raw = sig_type.type_token.raw;
                    static_fields_out[stat_idx].element_type = element_type;
                    static_fields_out[stat_idx].is_reference = is_ref;
                    static_fields_out[stat_idx].is_static = 1u;
                    static_fields_out[stat_idx].field_size = (uint8_t)field_size;
                    ++stat_idx;
                }
            }
            else
            {
                /* Instance field */
                current_offset = align_to(current_offset, alignment);

                if (inst_idx < instance_count)
                {
                    instance_fields[inst_idx].field_token_row = field_row;
                    instance_fields[inst_idx].byte_offset = current_offset;
                    instance_fields[inst_idx].nested_type_token_raw = sig_type.type_token.raw;
                    instance_fields[inst_idx].element_type = element_type;
                    instance_fields[inst_idx].is_reference = is_ref;
                    instance_fields[inst_idx].is_static = 0u;
                    instance_fields[inst_idx].field_size = (uint8_t)field_size;

                    if (is_ref != 0u)
                    {
                        out->gc_reference_count++;
                    }

                    ++inst_idx;
                }

                current_offset += field_size;
            }

            continue;

        cleanup_error:
            if (instance_fields != NULL)
            {
                kernel_free(instance_fields);
            }

            if (static_fields_out != NULL)
            {
                kernel_free(static_fields_out);
            }

            return result;
        }

        out->instance_fields = instance_fields;
        out->instance_field_count = instance_count;
        out->static_fields_layout = static_fields_out;
        out->static_field_count = static_count;
        out->own_instance_bytes = current_offset - parent_instance_bytes;

        return zaclr_result_ok();
    }

    static struct zaclr_result build_vtable(const struct zaclr_loaded_assembly* assembly,
                                            const struct zaclr_type_desc* type_desc,
                                            const struct zaclr_method_table* parent,
                                            const struct zaclr_method_desc*** out_vtable,
                                            uint32_t* out_slot_count)
    {
        uint32_t parent_slot_count = 0u;
        uint32_t new_virtual_count = 0u;
        uint32_t total_slot_count;
        const struct zaclr_method_desc** vtable;
        uint32_t method_index;
        uint32_t new_slot_index;

        *out_vtable = NULL;
        *out_slot_count = 0u;

        if (parent != NULL)
        {
            parent_slot_count = parent->vtable_slot_count;
        }

        /* Count new virtual methods in this type */
        for (method_index = 0u; method_index < type_desc->method_count; ++method_index)
        {
            uint32_t absolute_index = type_desc->first_method_index + method_index;
            if (absolute_index >= assembly->method_map.count)
            {
                break;
            }

            const struct zaclr_method_desc* method = &assembly->method_map.methods[absolute_index];
            if ((method->method_flags & METHOD_FLAG_VIRTUAL) == 0u)
            {
                continue;
            }

            if ((method->method_flags & METHOD_FLAG_NEW_SLOT) != 0u)
            {
                ++new_virtual_count;
            }
        }

        total_slot_count = parent_slot_count + new_virtual_count;
        if (total_slot_count == 0u)
        {
            return zaclr_result_ok();
        }

        vtable = (const struct zaclr_method_desc**)kernel_alloc(sizeof(const struct zaclr_method_desc*) * total_slot_count);
        if (vtable == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_METADATA);
        }

        kernel_memset(vtable, 0, sizeof(const struct zaclr_method_desc*) * total_slot_count);

        /* Copy parent vtable slots */
        if (parent != NULL && parent->vtable != NULL && parent_slot_count != 0u)
        {
            kernel_memcpy(vtable, parent->vtable, sizeof(const struct zaclr_method_desc*) * parent_slot_count);
        }

        /* Process virtual methods: override existing or append new slots */
        new_slot_index = parent_slot_count;
        for (method_index = 0u; method_index < type_desc->method_count; ++method_index)
        {
            uint32_t absolute_index = type_desc->first_method_index + method_index;
            if (absolute_index >= assembly->method_map.count)
            {
                break;
            }

            const struct zaclr_method_desc* method = &assembly->method_map.methods[absolute_index];
            if ((method->method_flags & METHOD_FLAG_VIRTUAL) == 0u)
            {
                continue;
            }

            if ((method->method_flags & METHOD_FLAG_NEW_SLOT) != 0u)
            {
                /* New virtual method: append to end of vtable */
                if (new_slot_index < total_slot_count)
                {
                    vtable[new_slot_index] = method;
                    ++new_slot_index;
                }
            }
            else
            {
                /* Override: find matching parent slot by name */
                uint32_t slot_index;
                bool found = false;

                for (slot_index = 0u; slot_index < parent_slot_count; ++slot_index)
                {
                    if (vtable[slot_index] != NULL
                        && vtable[slot_index]->name.text != NULL
                        && method->name.text != NULL
                        && text_equals(vtable[slot_index]->name.text, method->name.text))
                    {
                        vtable[slot_index] = method;
                        found = true;
                        break;
                    }
                }

                /* If no parent slot found but this is marked virtual without newslot,
                   treat it as a new slot to avoid losing the method. */
                if (!found && new_slot_index < total_slot_count)
                {
                    vtable[new_slot_index] = method;
                    ++new_slot_index;
                }
            }
        }

        *out_vtable = vtable;
        *out_slot_count = total_slot_count;
        return zaclr_result_ok();
    }
}

extern "C" struct zaclr_result zaclr_type_prepare(struct zaclr_runtime* runtime,
                                                   struct zaclr_loaded_assembly* assembly,
                                                   const struct zaclr_type_desc* type_desc,
                                                   struct zaclr_method_table** out_method_table)
{
    struct zaclr_method_table* mt = NULL;
    struct zaclr_method_table* parent_mt = NULL;
    struct zaclr_result result;
    uint32_t typedef_row;
    uint32_t cache_index;
    uint32_t mt_flags;
    struct field_layout_result fields = {};
    uint32_t parent_instance_bytes;
    uint32_t instance_size;

    if (runtime == NULL || assembly == NULL || type_desc == NULL || out_method_table == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    *out_method_table = NULL;
    typedef_row = zaclr_token_row(&type_desc->token);

    if (typedef_row == 0u || typedef_row > assembly->method_table_cache_count)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    cache_index = typedef_row - 1u;

    /* Check if already prepared */
    if (assembly->method_table_cache != NULL && assembly->method_table_cache[cache_index] != NULL)
    {
        mt = assembly->method_table_cache[cache_index];
        if (mt->preparation_state == ZACLR_MT_PREP_COMPLETE)
        {
            *out_method_table = mt;
            return zaclr_result_ok();
        }

        if (mt->preparation_state == ZACLR_MT_PREP_IN_PROGRESS)
        {
            /* Circular dependency detected. Return what we have so far. */
            *out_method_table = mt;
            return zaclr_result_ok();
        }
    }

    /* Allocate the MethodTable */
    mt = (struct zaclr_method_table*)kernel_alloc(sizeof(struct zaclr_method_table));
    if (mt == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_METADATA);
    }

    kernel_memset(mt, 0, sizeof(struct zaclr_method_table));
    mt->preparation_state = ZACLR_MT_PREP_IN_PROGRESS;
    mt->assembly = assembly;
    mt->type_desc = type_desc;

    /* Cache early to handle circular dependencies */
    if (assembly->method_table_cache != NULL)
    {
        assembly->method_table_cache[cache_index] = mt;
    }

    /* Step 1: Resolve and prepare parent type */
    result = resolve_parent_method_table(runtime, assembly, type_desc, &parent_mt);
    if (result.status != ZACLR_STATUS_OK)
    {
        /* Parent resolution failure is non-fatal for types that may not have
           a resolvable parent in the current assembly set (e.g. System.Object). */
        parent_mt = NULL;
    }

    mt->parent = parent_mt;

    /* Step 2: Compute flags */
    mt_flags = compute_type_flags(runtime, assembly, type_desc, parent_mt);
    mt->flags = mt_flags;

    /* Step 3: Compute field layout */
    parent_instance_bytes = 0u;
    if (parent_mt != NULL)
    {
        parent_instance_bytes = parent_mt->instance_size > ZACLR_OBJECT_HEADER_SIZE
            ? (parent_mt->instance_size - ZACLR_OBJECT_HEADER_SIZE)
            : 0u;
    }

    mt->base_instance_size = parent_instance_bytes;

    result = compute_field_layouts(runtime, assembly, type_desc, parent_instance_bytes, &fields);
    if (result.status != ZACLR_STATUS_OK)
    {
        /* Non-fatal: leave fields empty */
        fields = {};
    }

    mt->instance_fields = fields.instance_fields;
    mt->instance_field_count = fields.instance_field_count;
    mt->static_fields = fields.static_fields_layout;
    mt->static_field_count = fields.static_field_count;
    mt->gc_reference_field_count = fields.gc_reference_count;

    if (fields.gc_reference_count > 0u)
    {
        mt->flags |= ZACLR_MT_FLAG_CONTAINS_REFERENCES;
    }

    mt->field_alignment_requirement = fields.alignment_requirement != 0u ? fields.alignment_requirement : 1u;

    /* Step 4: Compute instance size */
    instance_size = ZACLR_OBJECT_HEADER_SIZE + parent_instance_bytes + fields.own_instance_bytes;

    /* Align instance size to pointer boundary */
    instance_size = align_to(instance_size, 8u);

    /* Minimum instance size is the object header */
    if (instance_size < ZACLR_OBJECT_HEADER_SIZE)
    {
        instance_size = ZACLR_OBJECT_HEADER_SIZE;
    }

    mt->instance_size = instance_size;

    /* Step 5: Component size for strings */
    if ((mt->flags & ZACLR_MT_FLAG_IS_STRING) != 0u)
    {
        mt->component_size = 2u; /* sizeof(char16_t) */
    }

    /* Step 6: Build vtable */
    {
        const struct zaclr_method_desc** vtable = NULL;
        uint32_t vtable_count = 0u;

        result = build_vtable(assembly, type_desc, parent_mt, &vtable, &vtable_count);
        if (result.status == ZACLR_STATUS_OK)
        {
            mt->vtable = vtable;
            mt->vtable_slot_count = vtable_count;
        }
    }

    /* Step 7: Interface map (stub) */
    mt->interface_count = 0u;

    /* Mark preparation as complete */
    mt->preparation_state = ZACLR_MT_PREP_COMPLETE;
    *out_method_table = mt;

    return zaclr_result_ok();
}
