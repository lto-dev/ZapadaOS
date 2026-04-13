#ifndef ZACLR_CALL_TARGET_H
#define ZACLR_CALL_TARGET_H

#include <kernel/zaclr/metadata/zaclr_method_map.h>
#include <kernel/zaclr/typesystem/zaclr_type_identity.h>
#include <kernel/zaclr/typesystem/zaclr_type_system.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_runtime;

struct zaclr_call_target {
    const struct zaclr_loaded_assembly* assembly;
    const struct zaclr_type_desc* owning_type;
    const struct zaclr_method_desc* method;
    struct zaclr_typespec_desc owning_typespec;
    struct zaclr_generic_context method_generic_context;
    uint8_t has_owning_typespec;
    uint8_t has_method_instantiation;
    uint16_t reserved0;
};

void zaclr_call_target_reset(struct zaclr_call_target* target);
struct zaclr_result zaclr_call_target_clone(struct zaclr_call_target* destination,
                                            const struct zaclr_call_target* source);
struct zaclr_result zaclr_call_target_resolve_memberref(const struct zaclr_runtime* runtime,
                                                        const struct zaclr_loaded_assembly* source_assembly,
                                                        const struct zaclr_memberref_target* memberref,
                                                        const struct zaclr_slice* methodspec_instantiation,
                                                        struct zaclr_call_target* out_target);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_CALL_TARGET_H */
