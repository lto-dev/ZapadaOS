#include <kernel/zaclr/process/zaclr_process_table.h>

extern "C" struct zaclr_result zaclr_process_table_initialize(struct zaclr_process_table* table)
{
    if (table == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    for (uint32_t i = 0u; i < ZACLR_PROCESS_TABLE_MAX_ENTRIES; ++i)
    {
        table->entries[i].pid = 0u;
        table->entries[i].ppid = 0u;
        table->entries[i].domain_id = 0u;
        table->entries[i].state = ZACLR_PROCESS_STATE_FREE;
        table->entries[i].uid = 0u;
        table->entries[i].gid = 0u;
        table->entries[i].exit_code = 0;
        table->entries[i].flags = 0u;
        table->entries[i].image_name[0] = '\0';
    }

    table->count = 0u;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_process_table_register(struct zaclr_process_table* table,
                                                             zaclr_process_id pid,
                                                             zaclr_process_id ppid,
                                                             zaclr_app_domain_id domain_id,
                                                             zaclr_user_id uid,
                                                             zaclr_group_id gid,
                                                             const char* image_name)
{
    struct zaclr_process_entry* entry;
    uint32_t slot;

    if (table == NULL || pid == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    if (table->count >= ZACLR_PROCESS_TABLE_MAX_ENTRIES)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    for (uint32_t i = 0u; i < ZACLR_PROCESS_TABLE_MAX_ENTRIES; ++i)
    {
        if (table->entries[i].pid == pid)
        {
            return zaclr_result_make(ZACLR_STATUS_ALREADY_EXISTS, ZACLR_STATUS_CATEGORY_PROCESS);
        }
    }

    slot = ZACLR_PROCESS_TABLE_MAX_ENTRIES;
    for (uint32_t i = 0u; i < ZACLR_PROCESS_TABLE_MAX_ENTRIES; ++i)
    {
        if (table->entries[i].state == ZACLR_PROCESS_STATE_FREE)
        {
            slot = i;
            break;
        }
    }

    if (slot >= ZACLR_PROCESS_TABLE_MAX_ENTRIES)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    entry = &table->entries[slot];
    entry->pid = pid;
    entry->ppid = ppid;
    entry->domain_id = domain_id;
    entry->state = ZACLR_PROCESS_STATE_CREATED;
    entry->uid = uid;
    entry->gid = gid;
    entry->exit_code = 0;
    entry->flags = 0u;

    if (image_name != NULL)
    {
        uint32_t j = 0u;
        while (image_name[j] != '\0' && j < (ZACLR_PROCESS_IMAGE_NAME_MAX - 1u))
        {
            entry->image_name[j] = image_name[j];
            ++j;
        }
        entry->image_name[j] = '\0';
    }
    else
    {
        entry->image_name[0] = '\0';
    }

    ++table->count;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_process_table_set_state(struct zaclr_process_table* table,
                                                              zaclr_process_id pid,
                                                              enum zaclr_process_state state)
{
    if (table == NULL || pid == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    for (uint32_t i = 0u; i < ZACLR_PROCESS_TABLE_MAX_ENTRIES; ++i)
    {
        if (table->entries[i].pid == pid && table->entries[i].state != ZACLR_PROCESS_STATE_FREE)
        {
            table->entries[i].state = state;
            return zaclr_result_ok();
        }
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_PROCESS);
}

extern "C" struct zaclr_result zaclr_process_table_set_exit_code(struct zaclr_process_table* table,
                                                                  zaclr_process_id pid,
                                                                  int32_t exit_code)
{
    if (table == NULL || pid == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    for (uint32_t i = 0u; i < ZACLR_PROCESS_TABLE_MAX_ENTRIES; ++i)
    {
        if (table->entries[i].pid == pid && table->entries[i].state != ZACLR_PROCESS_STATE_FREE)
        {
            table->entries[i].exit_code = exit_code;
            return zaclr_result_ok();
        }
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_PROCESS);
}

extern "C" const struct zaclr_process_entry* zaclr_process_table_find(const struct zaclr_process_table* table,
                                                                       zaclr_process_id pid)
{
    if (table == NULL || pid == 0u)
    {
        return NULL;
    }

    for (uint32_t i = 0u; i < ZACLR_PROCESS_TABLE_MAX_ENTRIES; ++i)
    {
        if (table->entries[i].pid == pid && table->entries[i].state != ZACLR_PROCESS_STATE_FREE)
        {
            return &table->entries[i];
        }
    }

    return NULL;
}

extern "C" uint32_t zaclr_process_table_count(const struct zaclr_process_table* table)
{
    if (table == NULL)
    {
        return 0u;
    }

    return table->count;
}

extern "C" const char* zaclr_process_state_name(enum zaclr_process_state state)
{
    switch (state)
    {
        case ZACLR_PROCESS_STATE_FREE:    return "free";
        case ZACLR_PROCESS_STATE_CREATED: return "created";
        case ZACLR_PROCESS_STATE_READY:   return "ready";
        case ZACLR_PROCESS_STATE_RUNNING: return "running";
        case ZACLR_PROCESS_STATE_BLOCKED: return "blocked";
        case ZACLR_PROCESS_STATE_EXITED:  return "exited";
        case ZACLR_PROCESS_STATE_FAILED:  return "failed";
        case ZACLR_PROCESS_STATE_ZOMBIE:  return "zombie";
        default:                          return "unknown";
    }
}
