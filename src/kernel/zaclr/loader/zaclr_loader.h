#ifndef ZACLR_LOADER_H
#define ZACLR_LOADER_H

#include <kernel/zaclr/loader/zaclr_pe_image.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>
#include <kernel/zaclr/exec/zaclr_eval_stack.h>
#include <kernel/zaclr/typesystem/zaclr_generic_context.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_method_table;

struct zaclr_generic_static_field_slot {
    uint32_t field_row;
    uint8_t initializer_state;
    uint8_t reserved0;
    uint16_t reserved1;
    struct zaclr_generic_context owner_context;
    struct zaclr_stack_value value;
    struct zaclr_generic_static_field_slot* next;
};

struct zaclr_loaded_assembly {
    zaclr_assembly_id id;
    struct zaclr_pe_image image;
    struct zaclr_metadata_reader metadata;
    struct zaclr_method_map method_map;
    struct zaclr_type_map type_map;
    char* assembly_name_storage;
    struct zaclr_stack_value* static_fields;
    struct zaclr_generic_static_field_slot* generic_static_fields;
    zaclr_object_handle* runtime_type_cache;
    uint8_t* type_initializer_state;
    struct zaclr_method_table** method_table_cache;  /* indexed by (typedef_row - 1) */
    uint32_t static_field_count;
    uint32_t generic_static_field_count;
    uint32_t runtime_type_cache_count;
    uint32_t method_table_cache_count;
    struct zaclr_name_view assembly_name;
    struct zaclr_token entry_point_token;
    uint32_t flags;
    /* Cached managed RuntimeAssembly object handle.  Mirrors CoreCLR's
       Assembly::m_ExposedObject pattern (see vm/assembly.hpp).
       0 = not yet created. */
    zaclr_object_handle exposed_assembly_handle;
};

struct zaclr_loader {
    zaclr_assembly_id next_assembly_id;
    uint32_t flags;
};

struct zaclr_result zaclr_loader_initialize(struct zaclr_loader* loader);
struct zaclr_result zaclr_loader_load_image(struct zaclr_loader* loader,
                                            const struct zaclr_slice* image,
                                            struct zaclr_loaded_assembly* loaded_assembly);
struct zaclr_result zaclr_loader_apply_assembly_name_fallback(struct zaclr_loaded_assembly* loaded_assembly,
                                                              const char* image_path);
void zaclr_loader_release_loaded_assembly(struct zaclr_loaded_assembly* loaded_assembly);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_LOADER_H */
