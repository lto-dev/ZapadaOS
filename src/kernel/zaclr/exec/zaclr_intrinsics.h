#ifndef ZACLR_INTRINSICS_H
#define ZACLR_INTRINSICS_H

#include <kernel/zaclr/include/zaclr_status.h>
#include <kernel/zaclr/loader/zaclr_loader.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_frame;
struct zaclr_type_desc;
struct zaclr_method_desc;
struct zaclr_loaded_assembly;

struct zaclr_result zaclr_try_invoke_intrinsic(struct zaclr_frame* frame,
                                               const struct zaclr_loaded_assembly* assembly,
                                               const struct zaclr_type_desc* type,
                                               const struct zaclr_method_desc* method);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_INTRINSICS_H */
