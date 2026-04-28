#ifndef ZACLR_DELEGATE_RUNTIME_H
#define ZACLR_DELEGATE_RUNTIME_H

#include <kernel/zaclr/exec/zaclr_eval_stack.h>
#include <kernel/zaclr/metadata/zaclr_token.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_runtime;
struct zaclr_loaded_assembly;
struct zaclr_type_desc;
struct zaclr_method_desc;
struct zaclr_method_handle;

struct zaclr_delegate_field_tokens {
    struct zaclr_token target_field;
    struct zaclr_token method_base_field;
    struct zaclr_token method_ptr_field;
    struct zaclr_token method_ptr_aux_field;
    struct zaclr_token invocation_list_field;
    struct zaclr_token invocation_count_field;
};

struct zaclr_result zaclr_delegate_runtime_resolve_field_tokens(struct zaclr_runtime* runtime,
                                                                const struct zaclr_loaded_assembly* assembly,
                                                                const struct zaclr_type_desc* delegate_type,
                                                                struct zaclr_delegate_field_tokens* out_tokens);
struct zaclr_result zaclr_delegate_runtime_bind_singlecast(struct zaclr_runtime* runtime,
                                                           struct zaclr_object_desc* delegate_object,
                                                           const struct zaclr_loaded_assembly* assembly,
                                                           const struct zaclr_type_desc* delegate_type,
                                                           const struct zaclr_stack_value* target_value,
                                                           const struct zaclr_method_handle* method_handle);
struct zaclr_result zaclr_delegate_runtime_combine(struct zaclr_runtime* runtime,
                                                   const struct zaclr_stack_value* left,
                                                   const struct zaclr_stack_value* right,
                                                   struct zaclr_stack_value* out_value);
struct zaclr_result zaclr_delegate_runtime_remove(struct zaclr_runtime* runtime,
                                                  const struct zaclr_stack_value* source,
                                                  const struct zaclr_stack_value* value,
                                                  struct zaclr_stack_value* out_value);
struct zaclr_result zaclr_delegate_runtime_equals(struct zaclr_runtime* runtime,
                                                  const struct zaclr_stack_value* left,
                                                  const struct zaclr_stack_value* right,
                                                  uint8_t* out_equal);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_DELEGATE_RUNTIME_H */
