#include <kernel/zaclr/exec/zaclr_engine.h>

#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

extern "C" {
#include <kernel/console.h>
}

extern "C" struct zaclr_result zaclr_engine_initialize(struct zaclr_engine* engine)
{
    if (engine == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    engine->next_frame_id = 1u;
    engine->flags = 0u;
    return zaclr_result_ok();
}

namespace
{
    static void zaclr_engine_log_dispatch_failure(const struct zaclr_frame* frame,
                                                  uint16_t opcode_value,
                                                  const struct zaclr_result& result)
    {
        if (frame == NULL)
        {
            return;
        }

        console_write("[ZACLR] Dispatch failure assembly=");
        console_write(frame->assembly != NULL ? frame->assembly->assembly_name.text : "<null>");
        console_write(" method=");
        console_write(frame->method != NULL ? frame->method->name.text : "<null>");
        console_write(" il_offset=");
        console_write_dec((uint64_t)frame->il_offset);
        console_write(" opcode=0x");
        console_write_hex64((uint64_t)opcode_value);
        console_write(" status=");
        console_write_dec((uint64_t)result.status);
        console_write(" category=");
        console_write_dec((uint64_t)result.category);
        console_write("\n");
    }

    static struct zaclr_result zaclr_engine_execute_frame_loop(struct zaclr_engine* engine,
                                                               struct zaclr_runtime* runtime,
                                                               struct zaclr_launch_state* launch_state,
                                                               struct zaclr_frame* current_frame,
                                                               bool mark_boot_complete)
    {
        struct zaclr_result result;

        if (engine == NULL || runtime == NULL || launch_state == NULL || current_frame == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        while (current_frame != NULL)
        {
            struct zaclr_dispatch_context context = {};
            uint8_t raw_opcode;
            uint16_t opcode_value;

            runtime->active_frame = current_frame;

            if (current_frame->il_offset >= current_frame->il_size)
            {
                zaclr_frame_destroy(current_frame);
                runtime->active_frame = NULL;
                return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
            }

            raw_opcode = current_frame->il_start[current_frame->il_offset];
            opcode_value = raw_opcode;
            if (raw_opcode == 0xFEu)
            {
                if ((current_frame->il_offset + 1u) >= current_frame->il_size)
                {
                    zaclr_frame_destroy(current_frame);
                    runtime->active_frame = NULL;
                    return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
                }

                opcode_value = (uint16_t)(0xFE00u | current_frame->il_start[current_frame->il_offset + 1u]);
            }

            context.runtime = runtime;
            context.engine = engine;
            context.current_frame = &current_frame;

            result = zaclr_dispatch_step(&context, (enum zaclr_opcode)opcode_value);
            if (result.status != ZACLR_STATUS_OK)
            {
                zaclr_engine_log_dispatch_failure(current_frame, opcode_value, result);
                while (current_frame != NULL)
                {
                    struct zaclr_frame* parent = current_frame->parent;
                    zaclr_frame_destroy(current_frame);
                    current_frame = parent;
                }

                runtime->active_frame = NULL;
                return result;
            }
        }

        runtime->active_frame = NULL;

        if (mark_boot_complete)
        {
            runtime->state.flags |= ZACLR_RUNTIME_STATE_FLAG_BOOT_COMPLETED;
            runtime->state.boot_completed_method_id = launch_state->entry_method != NULL ? launch_state->entry_method->id : 0u;
            launch_state->thread.current_frame = 0u;
            launch_state->thread.state = ZACLR_THREAD_STATE_STOPPED;
        }

        return zaclr_result_ok();
    }
}

extern "C" struct zaclr_result zaclr_engine_execute_launch(struct zaclr_engine* engine,
                                                            struct zaclr_runtime* runtime,
                                                            struct zaclr_launch_state* launch_state)
{
    struct zaclr_frame* current_frame;
    struct zaclr_result result;

    if (engine == NULL || runtime == NULL || launch_state == NULL || launch_state->assembly == NULL || launch_state->entry_method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_frame_create_root(engine, runtime, launch_state, &current_frame);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    runtime->state.flags |= ZACLR_RUNTIME_STATE_FLAG_BOOT_EXECUTED;

    return zaclr_engine_execute_frame_loop(engine, runtime, launch_state, current_frame, true);
}

extern "C" struct zaclr_result zaclr_engine_execute_method(struct zaclr_engine* engine,
                                                              struct zaclr_runtime* runtime,
                                                              struct zaclr_launch_state* launch_state,
                                                              const struct zaclr_loaded_assembly* assembly,
                                                              const struct zaclr_method_desc* method)
{
    struct zaclr_frame* current_frame;
    struct zaclr_result result;

    if (engine == NULL || runtime == NULL || launch_state == NULL || assembly == NULL || method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_frame_create_child(engine,
                                      runtime,
                                      NULL,
                                      assembly,
                                      method,
                                      &current_frame);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_engine_execute_frame_loop(engine, runtime, launch_state, current_frame, false);
}

extern "C" struct zaclr_result zaclr_engine_execute_instance_method(struct zaclr_engine* engine,
                                                                       struct zaclr_runtime* runtime,
                                                                       struct zaclr_launch_state* launch_state,
                                                                       const struct zaclr_loaded_assembly* assembly,
                                                                       const struct zaclr_method_desc* method,
                                                                       zaclr_object_handle instance_handle)
{
    static constexpr uint8_t k_has_this_calling_convention = 0x20u;
    struct zaclr_frame* current_frame;
    struct zaclr_result result;

    if (engine == NULL || runtime == NULL || launch_state == NULL || assembly == NULL || method == NULL || instance_handle == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if ((method->signature.calling_convention & k_has_this_calling_convention) == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = zaclr_frame_create_child(engine,
                                      runtime,
                                      NULL,
                                      assembly,
                                      method,
                                      &current_frame);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (current_frame->argument_count == 0u || current_frame->arguments == NULL)
    {
        zaclr_frame_destroy(current_frame);
        return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
    }

    current_frame->arguments[0].kind = ZACLR_STACK_VALUE_OBJECT_HANDLE;
    current_frame->arguments[0].data.object_handle = instance_handle;
    return zaclr_engine_execute_frame_loop(engine, runtime, launch_state, current_frame, false);
}
