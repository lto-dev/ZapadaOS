#ifndef ZACLR_GC_H
#define ZACLR_GC_H

#include <kernel/zaclr/runtime/zaclr_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_result zaclr_gc_collect(struct zaclr_runtime* runtime);
uint32_t zaclr_gc_collection_count(const struct zaclr_runtime* runtime);
struct zaclr_result zaclr_gc_wait_for_pending_finalizers(struct zaclr_runtime* runtime);
struct zaclr_result zaclr_gc_suppress_finalize(struct zaclr_runtime* runtime, zaclr_object_handle handle);
struct zaclr_result zaclr_gc_reregister_for_finalize(struct zaclr_runtime* runtime, zaclr_object_handle handle);
void zaclr_gc_mark_object(struct zaclr_runtime* runtime,
                          struct zaclr_object_desc* object);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_GC_H */
