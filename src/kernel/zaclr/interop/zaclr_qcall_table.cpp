#include <kernel/zaclr/interop/zaclr_qcall_table.h>

namespace
{
    static bool text_equals(const char* left, const char* right)
    {
        uint32_t index = 0u;

        if (left == NULL || right == NULL)
        {
            return false;
        }

        while (left[index] != '\0' && right[index] != '\0')
        {
            if (left[index] != right[index])
            {
                return false;
            }

            ++index;
        }

        return left[index] == right[index];
    }
}

extern "C" struct zaclr_result zaclr_qcall_table_initialize(struct zaclr_qcall_table* table)
{
    if (table == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    *table = {};
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_qcall_table_register(struct zaclr_qcall_table* table,
                                                           const char* entry_point,
                                                           zaclr_native_frame_handler handler)
{
    if (table == NULL || entry_point == NULL || handler == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    for (uint32_t index = 0u; index < table->count; ++index)
    {
        if (text_equals(table->entries[index].entry_point, entry_point))
        {
            table->entries[index].handler = handler;
            return zaclr_result_ok();
        }
    }

    if (table->count >= ZACLR_QCALL_TABLE_MAX_ENTRIES)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    table->entries[table->count].entry_point = entry_point;
    table->entries[table->count].handler = handler;
    ++table->count;
    return zaclr_result_ok();
}

extern "C" zaclr_native_frame_handler zaclr_qcall_table_resolve(const struct zaclr_qcall_table* table,
                                                                 const char* entry_point)
{
    if (table == NULL || entry_point == NULL)
    {
        return NULL;
    }

    for (uint32_t index = 0u; index < table->count; ++index)
    {
        if (text_equals(table->entries[index].entry_point, entry_point))
        {
            return table->entries[index].handler;
        }
    }

    return NULL;
}
