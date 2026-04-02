#ifndef ZACLR_INTERNAL_CALL_REGISTRY_H
#define ZACLR_INTERNAL_CALL_REGISTRY_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_native_assembly_descriptor;
struct zaclr_loaded_assembly;
struct zaclr_type_desc;
struct zaclr_method_desc;

#define ZACLR_INTERNAL_CALL_REGISTRY_MAX_ASSEMBLIES 16u
#define ZACLR_INTERNAL_CALL_REGISTRY_ROW_CACHE_CAPACITY 64u

struct zaclr_internal_call_row_cache_entry {
    zaclr_assembly_id assembly_id;
    uint32_t method_row;
    const struct zaclr_native_bind_method* method;
};

struct zaclr_internal_call_registry {
    const struct zaclr_native_assembly_descriptor* assemblies[ZACLR_INTERNAL_CALL_REGISTRY_MAX_ASSEMBLIES];
    uint32_t assembly_count;
    struct zaclr_internal_call_row_cache_entry row_cache[ZACLR_INTERNAL_CALL_REGISTRY_ROW_CACHE_CAPACITY];
    uint32_t row_cache_next;
};

struct zaclr_result zaclr_internal_call_registry_initialize(struct zaclr_internal_call_registry* registry);
struct zaclr_result zaclr_internal_call_registry_register_assembly(
    struct zaclr_internal_call_registry* registry,
    const struct zaclr_native_assembly_descriptor* assembly);
struct zaclr_result zaclr_internal_call_registry_register_generated(struct zaclr_internal_call_registry* registry);
struct zaclr_result zaclr_internal_call_registry_find_assembly(
    const struct zaclr_internal_call_registry* registry,
    const char* assembly_name,
    const struct zaclr_native_assembly_descriptor** out_assembly);
struct zaclr_result zaclr_internal_call_registry_resolve_exact(
    struct zaclr_internal_call_registry* registry,
    const struct zaclr_loaded_assembly* owning_assembly,
    const struct zaclr_type_desc* owning_type,
    const struct zaclr_method_desc* method,
    struct zaclr_internal_call_resolution* out_resolution);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_INTERNAL_CALL_REGISTRY_H */
