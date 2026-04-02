#ifndef ZACLR_APP_DOMAIN_H
#define ZACLR_APP_DOMAIN_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_app_domain {
    zaclr_app_domain_id id;
    zaclr_process_id process;
    zaclr_assembly_set_id assemblies;
    zaclr_type_static_map_id type_statics;
    uint32_t flags;
};

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_APP_DOMAIN_H */
