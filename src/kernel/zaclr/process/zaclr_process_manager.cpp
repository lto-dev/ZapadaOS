#include <kernel/zaclr/process/zaclr_process_manager.h>

extern "C" struct zaclr_result zaclr_process_manager_initialize(struct zaclr_process_manager* manager)
{
    if (manager == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    manager->next_process_id = 1u;
    manager->next_thread_id = 1u;
    manager->next_domain_id = 1u;
    manager->next_security_context_id = 1u;
    manager->next_handle_table_id = 1u;
    manager->next_static_store_id = 1u;
    manager->next_thread_group_id = 1u;
    manager->next_address_space_id = 1u;
    manager->next_assembly_set_id = 1u;
    manager->next_type_static_map_id = 1u;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_process_manager_create_boot_process(struct zaclr_process_manager* manager,
                                                                          struct zaclr_process* process)
{
    if (manager == NULL || process == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    process->id = manager->next_process_id++;
    process->security_context = manager->next_security_context_id++;
    process->address_space = manager->next_address_space_id++;
    process->handle_table = manager->next_handle_table_id++;
    process->root_domain = manager->next_domain_id++;
    process->static_store = manager->next_static_store_id++;
    process->threads = manager->next_thread_group_id++;
    process->flags = 0u;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_process_manager_create_boot_launch(struct zaclr_process_manager* manager,
                                                                          const struct zaclr_launch_request* request,
                                                                          struct zaclr_launch_state* launch_state)
{
    struct zaclr_result result;

    if (manager == NULL || request == NULL || launch_state == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    *launch_state = {};

    result = zaclr_process_manager_create_boot_process(manager, &launch_state->process);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    launch_state->security_context.id = launch_state->process.security_context;
    launch_state->security_context.user = request->user;
    launch_state->security_context.group = request->group;
    launch_state->security_context.flags = 0u;

    result = zaclr_handle_table_initialize(&launch_state->handle_table,
                                           launch_state->process.handle_table,
                                           3u);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    launch_state->domain.id = launch_state->process.root_domain;
    launch_state->domain.process = launch_state->process.id;
    launch_state->domain.assemblies = manager->next_assembly_set_id++;
    launch_state->domain.type_statics = manager->next_type_static_map_id++;
    launch_state->domain.flags = 0u;

    launch_state->thread.id = manager->next_thread_id++;
    launch_state->thread.process = launch_state->process.id;
    launch_state->thread.domain = launch_state->domain.id;
    launch_state->thread.state = ZACLR_THREAD_STATE_READY;
    launch_state->thread.current_frame = 0u;
    launch_state->thread.current_exception = 0u;
    launch_state->thread.flags = 0u;

    launch_state->entry_point.assembly_id = 0u;
    launch_state->entry_point.method_id = 0u;
    launch_state->assembly = NULL;
    launch_state->entry_method = NULL;
    launch_state->image_path = request->image_path;
    launch_state->flags = request->flags;
    return zaclr_result_ok();
}
