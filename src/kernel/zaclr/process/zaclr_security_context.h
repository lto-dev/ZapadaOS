#ifndef ZACLR_SECURITY_CONTEXT_H
#define ZACLR_SECURITY_CONTEXT_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_security_context {
    zaclr_security_context_id id;
    zaclr_user_id user;
    zaclr_group_id group;
    uint32_t flags;
};

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_SECURITY_CONTEXT_H */
