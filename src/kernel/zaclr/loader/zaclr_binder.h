#ifndef ZACLR_BINDER_H
#define ZACLR_BINDER_H

#include <kernel/zaclr/runtime/zaclr_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_result zaclr_binder_load_assembly_by_name(struct zaclr_runtime* runtime,
                                                       const char* assembly_name,
                                                       const struct zaclr_loaded_assembly** out_assembly);
struct zaclr_result zaclr_binder_load_assembly_by_path(struct zaclr_runtime* runtime,
                                                       const char* image_path,
                                                       const struct zaclr_loaded_assembly** out_assembly);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_BINDER_H */
