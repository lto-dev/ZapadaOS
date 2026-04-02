#ifndef ZACLR_HANDLE_TABLE_H
#define ZACLR_HANDLE_TABLE_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_handle_table {
    zaclr_handle_table_id id;
    struct zaclr_gc_handle_entry* entries;
    uint32_t count;
    uint32_t capacity;
    uint32_t flags;
};

enum zaclr_gc_handle_kind {
    ZACLR_GC_HANDLE_KIND_STRONG = 0,
    ZACLR_GC_HANDLE_KIND_WEAK = 1,
    ZACLR_GC_HANDLE_KIND_WEAK_TRACK_RESURRECTION = 2,
    ZACLR_GC_HANDLE_KIND_PINNED = 3
};

struct zaclr_gc_handle_entry {
    zaclr_object_handle handle;
    uint8_t kind;
    uint8_t flags;
    uint16_t reserved;
};

struct zaclr_result zaclr_handle_table_initialize(struct zaclr_handle_table* table,
                                                  zaclr_handle_table_id id,
                                                  uint32_t capacity);
void zaclr_handle_table_reset(struct zaclr_handle_table* table);
struct zaclr_result zaclr_handle_table_store_ex(struct zaclr_handle_table* table,
                                                zaclr_object_handle handle,
                                                uint32_t kind,
                                                uint32_t* out_index);
struct zaclr_result zaclr_handle_table_store(struct zaclr_handle_table* table,
                                             zaclr_object_handle handle,
                                             uint32_t* out_index);
struct zaclr_result zaclr_handle_table_load(const struct zaclr_handle_table* table,
                                            uint32_t index,
                                            zaclr_object_handle* out_handle);
struct zaclr_result zaclr_handle_table_load_entry(const struct zaclr_handle_table* table,
                                                  uint32_t index,
                                                  struct zaclr_gc_handle_entry* out_entry);
uint32_t zaclr_handle_table_count(const struct zaclr_handle_table* table);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_HANDLE_TABLE_H */
