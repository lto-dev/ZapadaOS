#ifndef ZACLR_APP_DOMAIN_H
#define ZACLR_APP_DOMAIN_H

#include <kernel/zaclr/loader/zaclr_assembly_registry.h>
#include <kernel/zaclr/loader/zaclr_assembly_source.h>
#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_app_domain {
    zaclr_app_domain_id id;
    zaclr_process_id process;
    zaclr_assembly_set_id assemblies;
    zaclr_type_static_map_id type_statics;
    struct zaclr_assembly_registry registry;
    struct zaclr_assembly_source* source;
    uint32_t flags;
};

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_APP_DOMAIN_H */
