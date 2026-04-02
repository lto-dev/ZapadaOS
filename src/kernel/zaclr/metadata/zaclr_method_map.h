#ifndef ZACLR_METHOD_MAP_H
#define ZACLR_METHOD_MAP_H

#include <kernel/zaclr/metadata/zaclr_signature.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_metadata_reader;

struct zaclr_method_desc {
    zaclr_method_id id;
    struct zaclr_token token;
    struct zaclr_token owning_type_token;
    struct zaclr_name_view name;
    struct zaclr_signature_desc signature;
    uint32_t rva;
    uint16_t impl_flags;
    uint16_t method_flags;
    uint32_t param_list;
    uint32_t flags;
    struct zaclr_name_view pinvoke_import_name;
    struct zaclr_name_view pinvoke_module_name;
};

struct zaclr_method_map {
    struct zaclr_method_desc* methods;
    uint32_t count;
    uint32_t flags;
};

struct zaclr_result zaclr_method_map_initialize(struct zaclr_method_map* map,
                                                const struct zaclr_metadata_reader* reader);
const struct zaclr_method_desc* zaclr_method_map_find_by_token(const struct zaclr_method_map* map,
                                                               struct zaclr_token token);
void zaclr_method_map_reset(struct zaclr_method_map* map);
uint32_t zaclr_method_flags(const struct zaclr_method_desc* method);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_METHOD_MAP_H */
