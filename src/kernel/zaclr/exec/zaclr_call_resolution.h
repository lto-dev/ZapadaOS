#ifndef ZACLR_CALL_RESOLUTION_H
#define ZACLR_CALL_RESOLUTION_H

#include <kernel/zaclr/include/zaclr_status.h>
#include <kernel/zaclr/loader/zaclr_loader.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_runtime;
struct zaclr_loaded_assembly;
struct zaclr_type_desc;
struct zaclr_memberref_target;

struct zaclr_result zaclr_dispatch_resolve_type_desc(const struct zaclr_loaded_assembly* current_assembly,
                                                     struct zaclr_runtime* runtime,
                                                     struct zaclr_token token,
                                                     const struct zaclr_loaded_assembly** out_assembly,
                                                     const struct zaclr_type_desc** out_type);

bool zaclr_dispatch_is_system_object_ctor(const struct zaclr_memberref_target* memberref);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_CALL_RESOLUTION_H */
