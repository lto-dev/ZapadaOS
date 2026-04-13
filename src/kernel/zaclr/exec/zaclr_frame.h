#ifndef ZACLR_FRAME_H
#define ZACLR_FRAME_H

#include <kernel/zaclr/exec/zaclr_eval_stack.h>
#include <kernel/zaclr/exec/zaclr_engine.h>
#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/metadata/zaclr_method_map.h>
#include <kernel/zaclr/typesystem/zaclr_generic_context.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_frame {
    zaclr_frame_id id;
    struct zaclr_runtime* runtime;
    struct zaclr_frame* parent;
    const struct zaclr_loaded_assembly* assembly;
    const struct zaclr_method_desc* method;
    zaclr_thread_id thread_id;
    zaclr_process_id process_id;
    const uint8_t* il_start;
    uint32_t il_size;
    uint32_t il_offset;
    uint16_t max_stack;
    uint16_t argument_count;
    uint16_t local_count;
    struct zaclr_stack_value* arguments;
    struct zaclr_stack_value* locals;
    struct zaclr_exception_clause* exception_clauses;
    uint16_t exception_clause_count;
    struct zaclr_eval_stack eval_stack;
    struct zaclr_generic_context generic_context;
    uint32_t flags;
};

struct zaclr_exception_clause {
    uint32_t flags;
    uint32_t try_offset;
    uint32_t try_length;
    uint32_t handler_offset;
    uint32_t handler_length;
    uint32_t class_token;
};

struct zaclr_result zaclr_frame_create_root(struct zaclr_engine* engine,
                                            struct zaclr_runtime* runtime,
                                            struct zaclr_launch_state* launch_state,
                                            struct zaclr_frame** out_frame);
struct zaclr_result zaclr_frame_create_child(struct zaclr_engine* engine,
                                             struct zaclr_runtime* runtime,
                                             struct zaclr_frame* parent,
                                             const struct zaclr_loaded_assembly* assembly,
                                             const struct zaclr_method_desc* method,
                                             struct zaclr_frame** out_frame);
struct zaclr_result zaclr_frame_bind_arguments(struct zaclr_frame* frame,
                                               struct zaclr_eval_stack* caller_stack);
void zaclr_frame_destroy(struct zaclr_frame* frame);
uint32_t zaclr_frame_flags(const struct zaclr_frame* frame);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_FRAME_H */
