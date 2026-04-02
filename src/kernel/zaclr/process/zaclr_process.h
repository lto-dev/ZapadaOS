#ifndef ZACLR_PROCESS_H
#define ZACLR_PROCESS_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_process {
    zaclr_process_id id;
    zaclr_security_context_id security_context;
    zaclr_address_space_id address_space;
    zaclr_handle_table_id handle_table;
    zaclr_app_domain_id root_domain;
    zaclr_static_store_id static_store;
    zaclr_thread_group_id threads;
    uint32_t flags;
};

struct zaclr_thread {
    zaclr_thread_id id;
    zaclr_process_id process;
    zaclr_app_domain_id domain;
    enum zaclr_thread_state state;
    zaclr_frame_id current_frame;
    zaclr_object_handle current_exception;
    uint32_t flags;
};

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_PROCESS_H */
