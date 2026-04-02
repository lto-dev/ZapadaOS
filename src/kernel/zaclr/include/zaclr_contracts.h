#ifndef ZACLR_CONTRACTS_H
#define ZACLR_CONTRACTS_H

#include <kernel/zaclr/include/zaclr_opcodes.h>
#include <kernel/zaclr/include/zaclr_status.h>
#include <kernel/zaclr/include/zaclr_trace.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_runtime;
struct zaclr_host_vtable;
struct zaclr_process;
struct zaclr_thread;
struct zaclr_app_domain;
struct zaclr_loaded_assembly;
struct zaclr_method_desc;
struct zaclr_type_desc;
struct zaclr_frame;
struct zaclr_eval_stack;
struct zaclr_heap;

struct zaclr_bootstrap_contract {
    const struct zaclr_host_vtable* host;
    struct zaclr_trace_config trace;
};

struct zaclr_method_locator {
    zaclr_assembly_id assembly_id;
    zaclr_method_id method_id;
};

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_CONTRACTS_H */
