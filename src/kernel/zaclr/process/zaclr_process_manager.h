#ifndef ZACLR_PROCESS_MANAGER_H
#define ZACLR_PROCESS_MANAGER_H

#include <kernel/zaclr/process/zaclr_process.h>
#include <kernel/zaclr/process/zaclr_process_launch.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_process_manager {
    zaclr_process_id next_process_id;
    zaclr_thread_id next_thread_id;
    zaclr_app_domain_id next_domain_id;
    zaclr_security_context_id next_security_context_id;
    zaclr_handle_table_id next_handle_table_id;
    zaclr_static_store_id next_static_store_id;
    zaclr_thread_group_id next_thread_group_id;
    zaclr_address_space_id next_address_space_id;
    zaclr_assembly_set_id next_assembly_set_id;
    zaclr_type_static_map_id next_type_static_map_id;
};

struct zaclr_result zaclr_process_manager_initialize(struct zaclr_process_manager* manager);
struct zaclr_result zaclr_process_manager_create_boot_process(struct zaclr_process_manager* manager,
                                                              struct zaclr_process* process);
struct zaclr_result zaclr_process_manager_create_boot_launch(struct zaclr_process_manager* manager,
                                                             const struct zaclr_launch_request* request,
                                                             struct zaclr_launch_state* launch_state);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_PROCESS_MANAGER_H */
