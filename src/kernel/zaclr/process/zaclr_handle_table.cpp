#include <kernel/zaclr/process/zaclr_handle_table.h>

extern "C" {
#include <kernel/support/kernel_memory.h>
}

extern "C" struct zaclr_result zaclr_handle_table_initialize(struct zaclr_handle_table* table,
                                                               zaclr_handle_table_id id,
                                                               uint32_t capacity)
{
    if (table == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    *table = {};
    table->id = id;
    table->capacity = capacity;
    if (capacity != 0u)
    {
        table->entries = (struct zaclr_gc_handle_entry*)kernel_alloc(sizeof(struct zaclr_gc_handle_entry) * (size_t)capacity);
        if (table->entries == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_PROCESS);
        }

        kernel_memset(table->entries, 0, sizeof(struct zaclr_gc_handle_entry) * (size_t)capacity);
    }

    return zaclr_result_ok();
}

extern "C" void zaclr_handle_table_reset(struct zaclr_handle_table* table)
{
    if (table == NULL)
    {
        return;
    }

    if (table->entries != NULL)
    {
        kernel_free(table->entries);
    }

    *table = {};
}

extern "C" struct zaclr_result zaclr_handle_table_store_ex(struct zaclr_handle_table* table,
                                                              zaclr_object_handle handle,
                                                              uint32_t kind,
                                                              uint32_t* out_index)
{
    uint32_t index;
    uint32_t free_index = 0xFFFFFFFFu;

    if (table == NULL || out_index == NULL || handle == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    for (index = 0u; index < table->count; ++index)
    {
        if (table->entries[index].handle == 0u && free_index == 0xFFFFFFFFu)
        {
            free_index = index;
            continue;
        }

        if (table->entries[index].handle == handle && table->entries[index].kind == kind)
        {
            *out_index = index;
            return zaclr_result_ok();
        }
    }

    if (table->entries == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    if (free_index != 0xFFFFFFFFu)
    {
        table->entries[free_index].handle = handle;
        table->entries[free_index].kind = (uint8_t)kind;
        table->entries[free_index].flags = 0u;
        table->entries[free_index].reserved = 0u;
        *out_index = free_index;
        return zaclr_result_ok();
    }

    if (table->count >= table->capacity)
    {
        return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    table->entries[table->count].handle = handle;
    table->entries[table->count].kind = (uint8_t)kind;
    table->entries[table->count].flags = 0u;
    table->entries[table->count].reserved = 0u;
    *out_index = table->count;
    table->count++;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_handle_table_store(struct zaclr_handle_table* table,
                                                           zaclr_object_handle handle,
                                                           uint32_t* out_index)
{
    return zaclr_handle_table_store_ex(table, handle, ZACLR_GC_HANDLE_KIND_STRONG, out_index);
}

extern "C" struct zaclr_result zaclr_handle_table_load(const struct zaclr_handle_table* table,
                                                         uint32_t index,
                                                         zaclr_object_handle* out_handle)
{
    if (table == NULL || out_handle == NULL || index >= table->count)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    *out_handle = table->entries[index].handle;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_handle_table_load_entry(const struct zaclr_handle_table* table,
                                                               uint32_t index,
                                                               struct zaclr_gc_handle_entry* out_entry)
{
    if (table == NULL || out_entry == NULL || index >= table->count)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    *out_entry = table->entries[index];
    return zaclr_result_ok();
}


extern "C" uint32_t zaclr_handle_table_count(const struct zaclr_handle_table* table)
{
    return table != NULL ? table->count : 0u;
}

extern "C" uint32_t zaclr_handle_table_capacity(const struct zaclr_handle_table* table)
{
    return table != NULL ? table->capacity : 0u;
}
