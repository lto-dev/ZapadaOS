#ifndef ZACLR_GENERIC_CONTEXT_H
#define ZACLR_GENERIC_CONTEXT_H

#include <kernel/zaclr/include/zaclr_status.h>
#include <kernel/zaclr/metadata/zaclr_signature.h>
#include <kernel/zaclr/metadata/zaclr_token.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_runtime;
struct zaclr_loaded_assembly;
struct zaclr_slice;

enum zaclr_generic_argument_kind {
    ZACLR_GENERIC_ARGUMENT_KIND_NONE = 0,
    ZACLR_GENERIC_ARGUMENT_KIND_CONCRETE_TYPE = 1,
    ZACLR_GENERIC_ARGUMENT_KIND_PRIMITIVE = 2,
    ZACLR_GENERIC_ARGUMENT_KIND_TYPE_VAR = 3,
    ZACLR_GENERIC_ARGUMENT_KIND_METHOD_VAR = 4,
    ZACLR_GENERIC_ARGUMENT_KIND_GENERIC_INST = 5
};

struct zaclr_generic_argument {
    uint8_t kind;
    uint8_t element_type;
    uint16_t reserved0;
    struct zaclr_token token;
    const struct zaclr_loaded_assembly* assembly;
    uint32_t generic_param_index;
    uint32_t instantiation_arg_count;
    struct zaclr_generic_argument* instantiation_args;
};

struct zaclr_generic_context {
    uint32_t type_arg_count;
    struct zaclr_generic_argument* type_args;
    uint32_t method_arg_count;
    struct zaclr_generic_argument* method_args;
};

void zaclr_generic_context_reset(struct zaclr_generic_context* context);
struct zaclr_result zaclr_generic_context_clone(struct zaclr_generic_context* destination,
                                                const struct zaclr_generic_context* source);
struct zaclr_result zaclr_generic_context_assign_type_args(struct zaclr_generic_context* destination,
                                                           const struct zaclr_generic_context* source);
struct zaclr_result zaclr_generic_context_assign_method_args(struct zaclr_generic_context* destination,
                                                             const struct zaclr_generic_context* source);
struct zaclr_result zaclr_generic_context_set_method_instantiation(struct zaclr_generic_context* context,
                                                                   const struct zaclr_loaded_assembly* current_assembly,
                                                                   struct zaclr_runtime* runtime,
                                                                   const struct zaclr_slice* instantiation_blob);
struct zaclr_result zaclr_generic_context_set_type_instantiation(struct zaclr_generic_context* context,
                                                                 const struct zaclr_loaded_assembly* current_assembly,
                                                                 struct zaclr_runtime* runtime,
                                                                 const struct zaclr_slice* instantiation_blob);
const struct zaclr_generic_argument* zaclr_generic_context_get_type_argument(const struct zaclr_generic_context* context,
                                                                             uint32_t index);
const struct zaclr_generic_argument* zaclr_generic_context_get_method_argument(const struct zaclr_generic_context* context,
                                                                               uint32_t index);
struct zaclr_result zaclr_generic_context_resolve_signature_type(const struct zaclr_generic_context* context,
                                                                 const struct zaclr_loaded_assembly* current_assembly,
                                                                 struct zaclr_runtime* runtime,
                                                                 const struct zaclr_signature_type* signature_type,
                                                                 struct zaclr_generic_argument* out_argument);

/* Produce a new context whose type_args are a copy of source->type_args with any
   TYPE_VAR and METHOD_VAR entries substituted using the corresponding slot from
   substitution->type_args / substitution->method_args respectively.
   The returned context has method_arg_count == 0 (type args only). */
struct zaclr_result zaclr_generic_context_substitute_type_args(struct zaclr_generic_context* out_context,
                                                                 const struct zaclr_generic_context* source,
                                                                 const struct zaclr_generic_context* substitution);
struct zaclr_result zaclr_generic_context_substitute_method_args(struct zaclr_generic_context* out_context,
                                                                 const struct zaclr_generic_context* source,
                                                                 const struct zaclr_generic_context* substitution);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_GENERIC_CONTEXT_H */
