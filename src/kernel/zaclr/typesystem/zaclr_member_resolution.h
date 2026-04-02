#ifndef ZACLR_MEMBER_RESOLUTION_H
#define ZACLR_MEMBER_RESOLUTION_H

#include <kernel/zaclr/typesystem/zaclr_type_system.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_result zaclr_metadata_get_memberref_info(const struct zaclr_loaded_assembly* assembly,
                                                      struct zaclr_token token,
                                                      struct zaclr_memberref_target* out_info);
struct zaclr_result zaclr_member_resolution_resolve_method(const struct zaclr_runtime* runtime,
                                                           const struct zaclr_loaded_assembly* source_assembly,
                                                           const struct zaclr_memberref_target* memberref,
                                                           const struct zaclr_loaded_assembly** out_assembly,
                                                           const struct zaclr_type_desc** out_type,
                                                           const struct zaclr_method_desc** out_method);
struct zaclr_result zaclr_member_resolution_resolve_field(const struct zaclr_runtime* runtime,
                                                          const struct zaclr_memberref_target* memberref,
                                                          const struct zaclr_loaded_assembly** out_assembly,
                                                          uint32_t* out_field_row);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_MEMBER_RESOLUTION_H */
