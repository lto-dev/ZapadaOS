#ifndef ZACLR_HEAP_H
#define ZACLR_HEAP_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_object_desc;
struct zaclr_loaded_assembly;
struct zaclr_runtime;

enum {
    ZACLR_HEAP_FLAG_GC_ACTIVE = 0x00000001u
};

struct zaclr_heap_object_node {
    struct zaclr_object_desc* object;
    uint32_t allocation_size;
    uint8_t used;
    uint8_t reserved0;
    uint16_t reserved1;
};

struct zaclr_heap {
    struct zaclr_runtime* runtime;
    uint32_t flags;
    struct zaclr_heap_object_node* nodes;
    uint32_t node_count;
    uint32_t node_capacity;
    uint32_t live_object_count;
    uint32_t allocated_bytes;
    uint32_t collection_threshold_bytes;
    uint32_t collection_count;
};

struct zaclr_result zaclr_heap_initialize(struct zaclr_heap* heap,
                                          struct zaclr_runtime* runtime);
void zaclr_heap_reset(struct zaclr_heap* heap);
struct zaclr_result zaclr_heap_allocate_object(struct zaclr_heap* heap,
                                               size_t allocation_size,
                                               const struct zaclr_loaded_assembly* owning_assembly,
                                               zaclr_type_id type_id,
                                               uint32_t object_family,
                                               uint32_t object_flags,
                                               struct zaclr_object_desc** out_object);
struct zaclr_object_desc* zaclr_heap_get_object(const struct zaclr_heap* heap,
                                                zaclr_object_handle handle);
/* Handle conversion remains only as a compatibility bridge for GC handle tables
 * and interop/native boundaries. Runtime-internal heap/object paths should
 * prefer direct object pointers. */
zaclr_object_handle zaclr_heap_get_object_handle(const struct zaclr_heap* heap,
                                                 const struct zaclr_object_desc* object);
uint32_t zaclr_heap_live_object_count(const struct zaclr_heap* heap);
uint32_t zaclr_heap_allocated_bytes(const struct zaclr_heap* heap);
void zaclr_heap_clear_marks(struct zaclr_heap* heap);
void zaclr_heap_sweep_unmarked(struct zaclr_heap* heap);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_HEAP_H */
