#ifndef ZACLR_ASSEMBLY_REGISTRY_H
#define ZACLR_ASSEMBLY_REGISTRY_H

#include <kernel/zaclr/loader/zaclr_loader.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_assembly_registry {
    struct zaclr_loaded_assembly* entries;
    uint32_t count;
    uint32_t capacity;
    uint32_t flags;
};

struct zaclr_result zaclr_assembly_registry_initialize(struct zaclr_assembly_registry* registry);
struct zaclr_result zaclr_assembly_registry_register(struct zaclr_assembly_registry* registry,
                                                     const struct zaclr_loaded_assembly* assembly);
const struct zaclr_loaded_assembly* zaclr_assembly_registry_find_by_id(const struct zaclr_assembly_registry* registry,
                                                                       zaclr_assembly_id assembly_id);
const struct zaclr_loaded_assembly* zaclr_assembly_registry_find_by_name(const struct zaclr_assembly_registry* registry,
                                                                         const char* assembly_name);
void zaclr_assembly_registry_reset(struct zaclr_assembly_registry* registry);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_ASSEMBLY_REGISTRY_H */
