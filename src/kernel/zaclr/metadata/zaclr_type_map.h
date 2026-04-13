#ifndef ZACLR_TYPE_MAP_H
#define ZACLR_TYPE_MAP_H

#include <kernel/zaclr/metadata/zaclr_method_map.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_metadata_reader;

struct zaclr_type_desc {
    zaclr_type_id id;
    struct zaclr_token token;
    struct zaclr_name_view type_namespace;
    struct zaclr_name_view type_name;
    struct zaclr_token extends;
    uint32_t field_list;
    uint32_t field_count;
    uint32_t method_list;
    uint32_t first_method_index;
    uint32_t method_count;
    uint32_t flags;
    uint32_t runtime_flags;
};

enum zaclr_type_runtime_flags {
    ZACLR_TYPE_RUNTIME_FLAG_NONE = 0x00000000u,
    ZACLR_TYPE_RUNTIME_FLAG_HAS_FINALIZER = 0x00000001u,
    ZACLR_TYPE_RUNTIME_FLAG_HAS_COMPONENT_SIZE = 0x00000002u
};

struct zaclr_type_map {
    struct zaclr_type_desc* types;
    uint32_t count;
    uint32_t flags;
};

struct zaclr_result zaclr_type_map_initialize(struct zaclr_type_map* map,
                                              const struct zaclr_metadata_reader* reader,
                                              struct zaclr_method_map* methods);
const struct zaclr_type_desc* zaclr_type_map_find_by_token(const struct zaclr_type_map* map,
                                                           struct zaclr_token token);
void zaclr_type_map_reset(struct zaclr_type_map* map);
uint32_t zaclr_type_flags(const struct zaclr_type_desc* type);
uint32_t zaclr_type_runtime_flags(const struct zaclr_type_desc* type);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_TYPE_MAP_H */
