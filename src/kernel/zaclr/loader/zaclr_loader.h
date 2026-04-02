#ifndef ZACLR_LOADER_H
#define ZACLR_LOADER_H

#include <kernel/zaclr/loader/zaclr_pe_image.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>
#include <kernel/zaclr/exec/zaclr_eval_stack.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_loaded_assembly {
    zaclr_assembly_id id;
    struct zaclr_pe_image image;
    struct zaclr_metadata_reader metadata;
    struct zaclr_method_map method_map;
    struct zaclr_type_map type_map;
    struct zaclr_stack_value* static_fields;
    uint32_t static_field_count;
    struct zaclr_name_view assembly_name;
    struct zaclr_token entry_point_token;
    uint32_t flags;
};

struct zaclr_loader {
    zaclr_assembly_id next_assembly_id;
    uint32_t flags;
};

struct zaclr_result zaclr_loader_initialize(struct zaclr_loader* loader);
struct zaclr_result zaclr_loader_load_image(struct zaclr_loader* loader,
                                            const struct zaclr_slice* image,
                                            struct zaclr_loaded_assembly* loaded_assembly);
void zaclr_loader_release_loaded_assembly(struct zaclr_loaded_assembly* loaded_assembly);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_LOADER_H */
