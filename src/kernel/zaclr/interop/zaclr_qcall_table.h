#ifndef ZACLR_QCALL_TABLE_H
#define ZACLR_QCALL_TABLE_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

#define ZACLR_QCALL_TABLE_MAX_ENTRIES 128u

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_qcall_entry {
    const char* entry_point;
    zaclr_native_frame_handler handler;
};

struct zaclr_qcall_table {
    struct zaclr_qcall_entry entries[ZACLR_QCALL_TABLE_MAX_ENTRIES];
    uint32_t count;
};

struct zaclr_result zaclr_qcall_table_initialize(struct zaclr_qcall_table* table);
struct zaclr_result zaclr_qcall_table_register(struct zaclr_qcall_table* table,
                                               const char* entry_point,
                                               zaclr_native_frame_handler handler);
zaclr_native_frame_handler zaclr_qcall_table_resolve(const struct zaclr_qcall_table* table,
                                                     const char* entry_point);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_QCALL_TABLE_H */
