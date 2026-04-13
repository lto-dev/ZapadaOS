#ifndef ZACLR_TYPE_IDENTITY_H
#define ZACLR_TYPE_IDENTITY_H

#include <kernel/zaclr/metadata/zaclr_signature.h>
#include <kernel/zaclr/typesystem/zaclr_generic_context.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zaclr_type_identity_kind {
    ZACLR_TYPE_IDENTITY_KIND_NONE = 0,
    ZACLR_TYPE_IDENTITY_KIND_CONCRETE = 1,
    ZACLR_TYPE_IDENTITY_KIND_PRIMITIVE = 2,
    ZACLR_TYPE_IDENTITY_KIND_TYPE_VAR = 3,
    ZACLR_TYPE_IDENTITY_KIND_METHOD_VAR = 4,
    ZACLR_TYPE_IDENTITY_KIND_GENERIC_INST = 5
};

struct zaclr_type_identity {
    uint8_t kind;
    uint8_t element_type;
    uint16_t reserved0;
    struct zaclr_token token;
    const struct zaclr_loaded_assembly* assembly;
    uint32_t generic_param_index;
    uint32_t generic_arg_count;
    struct zaclr_type_identity* generic_args;
};

void zaclr_type_identity_reset(struct zaclr_type_identity* identity);
struct zaclr_result zaclr_type_identity_clone(struct zaclr_type_identity* destination,
                                              const struct zaclr_type_identity* source);
struct zaclr_result zaclr_type_identity_from_generic_argument(const struct zaclr_generic_argument* argument,
                                                              struct zaclr_type_identity* out_identity);
struct zaclr_result zaclr_type_identity_from_signature_type(const struct zaclr_generic_context* generic_context,
                                                            const struct zaclr_loaded_assembly* current_assembly,
                                                            struct zaclr_runtime* runtime,
                                                            const struct zaclr_signature_type* signature_type,
                                                            struct zaclr_type_identity* out_identity);
struct zaclr_result zaclr_type_identity_from_token(const struct zaclr_generic_context* generic_context,
                                                   const struct zaclr_loaded_assembly* current_assembly,
                                                   struct zaclr_runtime* runtime,
                                                   struct zaclr_token token,
                                                   struct zaclr_type_identity* out_identity);
struct zaclr_result zaclr_type_identity_materialize_runtime_type_handle(struct zaclr_runtime* runtime,
                                                                        const struct zaclr_type_identity* identity,
                                                                        zaclr_object_handle* out_handle);
struct zaclr_result zaclr_type_identity_from_runtime_type_handle(struct zaclr_runtime* runtime,
                                                                 zaclr_object_handle handle,
                                                                 struct zaclr_type_identity* out_identity);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_TYPE_IDENTITY_H */
