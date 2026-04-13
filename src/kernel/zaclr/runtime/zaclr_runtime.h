#ifndef ZACLR_RUNTIME_H
#define ZACLR_RUNTIME_H

#include <kernel/zaclr/interop/zaclr_internal_call_registry.h>
#include <kernel/zaclr/interop/zaclr_qcall_table.h>
#include <kernel/zaclr/loader/zaclr_assembly_registry.h>
#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/exec/zaclr_engine.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/process/zaclr_process_launch.h>
#include <kernel/zaclr/process/zaclr_process_manager.h>
#include <kernel/zaclr/runtime/zaclr_runtime_state.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_runtime {
    struct zaclr_runtime_state state;
    struct zaclr_process_manager process_manager;
    struct zaclr_loader loader;
    struct zaclr_assembly_registry assemblies;
    struct zaclr_internal_call_registry internal_calls;
    struct zaclr_qcall_table qcall_table;
    struct zaclr_heap heap;
    struct zaclr_handle_table finalizer_queue;
    struct zaclr_engine engine;
    struct zaclr_launch_state boot_launch;
    struct zaclr_frame* active_frame;
};

void zaclr_runtime_reset(struct zaclr_runtime* runtime);
struct zaclr_app_domain* zaclr_runtime_current_domain(struct zaclr_runtime* runtime);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_RUNTIME_H */
