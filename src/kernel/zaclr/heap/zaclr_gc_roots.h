#ifndef ZACLR_GC_ROOTS_H
#define ZACLR_GC_ROOTS_H

#include <kernel/zaclr/exec/zaclr_frame.h>
#include <kernel/zaclr/process/zaclr_process.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_object_desc;

enum zaclr_gc_root_flags {
    ZACLR_GC_ROOT_FLAG_THREAD_EXCEPTION = 0x00000001u,
    ZACLR_GC_ROOT_FLAG_ARGUMENT = 0x00000002u,
    ZACLR_GC_ROOT_FLAG_LOCAL = 0x00000004u,
    ZACLR_GC_ROOT_FLAG_EVAL_STACK = 0x00000008u,
    ZACLR_GC_ROOT_FLAG_STATIC = 0x00000010u,
    ZACLR_GC_ROOT_FLAG_SYNTHETIC_TEST = 0x00000020u,
    ZACLR_GC_ROOT_FLAG_HANDLE_TABLE = 0x00000040u
};

typedef void (*zaclr_gc_visit_object_reference_fn)(struct zaclr_object_desc** slot,
                                                   uint32_t flags,
                                                   void* context);

struct zaclr_gc_root_visitor {
    zaclr_gc_visit_object_reference_fn visit_object_reference;
    void* context;
};

void zaclr_gc_enumerate_frame_roots(struct zaclr_frame* frame,
                                    struct zaclr_gc_root_visitor* visitor);
void zaclr_gc_enumerate_thread_roots(struct zaclr_thread* thread,
                                     struct zaclr_frame* current_frame,
                                     struct zaclr_gc_root_visitor* visitor);
void zaclr_gc_enumerate_runtime_roots(struct zaclr_runtime* runtime,
                                      struct zaclr_gc_root_visitor* visitor);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_GC_ROOTS_H */
