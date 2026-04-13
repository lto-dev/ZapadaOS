#ifndef ZACLR_METHOD_TABLE_H
#define ZACLR_METHOD_TABLE_H

#include <kernel/zaclr/typesystem/zaclr_field_layout.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_loaded_assembly;
struct zaclr_type_desc;
struct zaclr_method_desc;

/* Preparation state for a method table */
enum zaclr_method_table_preparation_state {
    ZACLR_MT_PREP_NOT_STARTED = 0,
    ZACLR_MT_PREP_IN_PROGRESS = 1,
    ZACLR_MT_PREP_COMPLETE    = 2
};

/* Flags describing properties of a prepared type */
enum zaclr_method_table_flags {
    ZACLR_MT_FLAG_NONE                = 0x00000000u,
    ZACLR_MT_FLAG_HAS_FINALIZER       = 0x00000001u,
    ZACLR_MT_FLAG_HAS_CCTOR           = 0x00000002u,
    ZACLR_MT_FLAG_IS_VALUE_TYPE       = 0x00000004u,
    ZACLR_MT_FLAG_IS_ENUM             = 0x00000008u,
    ZACLR_MT_FLAG_IS_INTERFACE        = 0x00000010u,
    ZACLR_MT_FLAG_IS_ABSTRACT         = 0x00000020u,
    ZACLR_MT_FLAG_IS_SEALED           = 0x00000040u,
    ZACLR_MT_FLAG_HAS_COMPONENT_SIZE  = 0x00000080u,
    ZACLR_MT_FLAG_IS_STRING           = 0x00000100u,
    ZACLR_MT_FLAG_IS_ARRAY            = 0x00000200u,
    ZACLR_MT_FLAG_IS_DELEGATE         = 0x00000400u,
    ZACLR_MT_FLAG_CONTAINS_REFERENCES = 0x00000800u
};

/* Field layout descriptor - computed from metadata during type preparation */
struct zaclr_field_layout {
    uint32_t field_token_row;     /* 1-based row in Field table */
    uint32_t byte_offset;         /* offset from start of instance fields (after object header) */
    uint32_t nested_type_token_raw; /* TypeDef/TypeRef/TypeSpec token for valuetype fields */
    uint8_t element_type;         /* ELEMENT_TYPE from signature (I4, I8, CLASS, VALUETYPE, etc.) */
    uint8_t is_reference;         /* 1 if this field holds a managed object reference */
    uint8_t is_static;            /* 1 if this is a static field */
    uint8_t field_size;           /* size in bytes (1, 2, 4, 8, or pointer size for references) */
};

/* MethodTable - runtime representation of a prepared type */
struct zaclr_method_table {
    /* Identity */
    const struct zaclr_loaded_assembly* assembly;
    const struct zaclr_type_desc* type_desc;
    struct zaclr_method_table* parent;    /* NULL for System.Object */

    /* VTable */
    const struct zaclr_method_desc** vtable;
    uint32_t vtable_slot_count;

    /* Instance layout */
    uint32_t instance_size;               /* total bytes: object header + all instance fields */
    uint32_t base_instance_size;          /* bytes from parent type's fields */
    struct zaclr_field_layout* instance_fields;
    uint32_t instance_field_count;

    /* Static layout */
    struct zaclr_field_layout* static_fields;
    uint32_t static_field_count;

    /* GC descriptor */
    uint32_t gc_reference_field_count;    /* number of reference-type instance fields */

    /* Interface map (stub for now) */
    uint32_t interface_count;

    /* Flags */
    uint32_t flags;
    uint32_t component_size;              /* for arrays/strings: element size in bytes */
    uint32_t field_alignment_requirement; /* natural alignment for nested valuetype placement */

    /* Preparation state */
    uint8_t preparation_state;            /* see zaclr_method_table_preparation_state */
};

/* Accessor functions for MethodTable flag queries */
uint8_t zaclr_method_table_has_finalizer(const struct zaclr_method_table* mt);
uint8_t zaclr_method_table_is_value_type(const struct zaclr_method_table* mt);
uint8_t zaclr_method_table_is_interface(const struct zaclr_method_table* mt);
uint8_t zaclr_method_table_contains_references(const struct zaclr_method_table* mt);
uint32_t zaclr_method_table_instance_size(const struct zaclr_method_table* mt);
uint32_t zaclr_method_table_component_size(const struct zaclr_method_table* mt);
uint8_t zaclr_method_table_is_enum(const struct zaclr_method_table* mt);
uint8_t zaclr_method_table_is_abstract(const struct zaclr_method_table* mt);
uint8_t zaclr_method_table_is_sealed(const struct zaclr_method_table* mt);
uint8_t zaclr_method_table_has_cctor(const struct zaclr_method_table* mt);
uint8_t zaclr_method_table_is_string(const struct zaclr_method_table* mt);
uint8_t zaclr_method_table_is_array(const struct zaclr_method_table* mt);
uint8_t zaclr_method_table_is_delegate(const struct zaclr_method_table* mt);
uint8_t zaclr_method_table_is_prepared(const struct zaclr_method_table* mt);

/* Virtual dispatch: given a method declared on a base type and the runtime
   object's method table, find the vtable override.  Returns the override
   method_desc, or the original if no vtable entry matches (non-virtual). */
const struct zaclr_method_desc* zaclr_method_table_resolve_virtual(
    const struct zaclr_method_table* runtime_mt,
    const struct zaclr_method_desc* declared_method);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_METHOD_TABLE_H */
