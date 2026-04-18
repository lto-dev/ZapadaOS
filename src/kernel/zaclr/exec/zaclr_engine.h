#ifndef ZACLR_ENGINE_H
#define ZACLR_ENGINE_H

#include <kernel/zaclr/exec/zaclr_dispatch.h>
#include <kernel/zaclr/exec/zaclr_thread.h>
#include <kernel/zaclr/exec/zaclr_exceptions.h>
#include <kernel/zaclr/process/zaclr_process_launch.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_engine {
    zaclr_frame_id next_frame_id;
    uint32_t flags;
};

struct zaclr_result zaclr_engine_initialize(struct zaclr_engine* engine);
struct zaclr_result zaclr_engine_execute_launch(struct zaclr_engine* engine,
                                                struct zaclr_runtime* runtime,
                                                struct zaclr_launch_state* launch_state);
struct zaclr_result zaclr_engine_execute_method(struct zaclr_engine* engine,
                                                struct zaclr_runtime* runtime,
                                                struct zaclr_launch_state* launch_state,
                                                const struct zaclr_loaded_assembly* assembly,
                                                const struct zaclr_method_desc* method);
struct zaclr_result zaclr_engine_execute_method_with_type_context(struct zaclr_engine* engine,
                                                                   struct zaclr_runtime* runtime,
                                                                   struct zaclr_launch_state* launch_state,
                                                                   const struct zaclr_loaded_assembly* assembly,
                                                                   const struct zaclr_method_desc* method,
                                                                   const struct zaclr_generic_context* type_context);
struct zaclr_result zaclr_engine_execute_instance_method(struct zaclr_engine* engine,
                                                          struct zaclr_runtime* runtime,
                                                          struct zaclr_launch_state* launch_state,
                                                          const struct zaclr_loaded_assembly* assembly,
                                                          const struct zaclr_method_desc* method,
                                                          struct zaclr_object_desc* instance);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_ENGINE_H */
