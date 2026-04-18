#ifndef ZACLR_TYPE_INIT_H
#define ZACLR_TYPE_INIT_H

#include <kernel/zaclr/include/zaclr_status.h>
#include <kernel/zaclr/loader/zaclr_loader.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_runtime;
struct zaclr_loaded_assembly;
struct zaclr_type_desc;
struct zaclr_method_desc;
struct zaclr_generic_context;

const struct zaclr_method_desc* zaclr_find_type_initializer(const struct zaclr_loaded_assembly* assembly,
                                                             const struct zaclr_type_desc* type);

struct zaclr_result zaclr_ensure_type_initializer_ran(struct zaclr_runtime* runtime,
                                                       const struct zaclr_loaded_assembly* assembly,
                                                       const struct zaclr_type_desc* type);

/* Like zaclr_ensure_type_initializer_ran but passes type_context into the .cctor frame
   so that generic type parameters are visible inside the static constructor body. */
struct zaclr_result zaclr_ensure_type_initializer_ran_with_context(struct zaclr_runtime* runtime,
                                                                    const struct zaclr_loaded_assembly* assembly,
                                                                    const struct zaclr_type_desc* type,
                                                                    const struct zaclr_generic_context* type_context);

bool zaclr_method_is_type_initializer_trigger(const struct zaclr_method_desc* method);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_TYPE_INIT_H */
