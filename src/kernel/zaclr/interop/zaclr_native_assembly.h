#ifndef ZACLR_NATIVE_ASSEMBLY_H
#define ZACLR_NATIVE_ASSEMBLY_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_loaded_assembly;
struct zaclr_method_desc;
struct zaclr_signature_desc;
struct zaclr_type_desc;

struct zaclr_native_assembly_descriptor {
    const char* assembly_name;
    const struct zaclr_native_bind_method* method_lookup;
    uint32_t method_count;
};

const char* zaclr_native_assembly_name(const struct zaclr_native_assembly_descriptor* assembly);
const struct zaclr_native_bind_method* zaclr_native_assembly_method_lookup(const struct zaclr_native_assembly_descriptor* assembly);
uint32_t zaclr_native_assembly_method_count(const struct zaclr_native_assembly_descriptor* assembly);
bool zaclr_native_bind_method_matches_managed(const struct zaclr_loaded_assembly* assembly,
                                              const struct zaclr_type_desc* owning_type,
                                              const struct zaclr_method_desc* method,
                                              const struct zaclr_native_bind_method* candidate);
bool zaclr_managed_signatures_equal(const struct zaclr_loaded_assembly* left_assembly,
                                    const struct zaclr_signature_desc* left_signature,
                                    const struct zaclr_loaded_assembly* right_assembly,
                                    const struct zaclr_signature_desc* right_signature);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_NATIVE_ASSEMBLY_H */
