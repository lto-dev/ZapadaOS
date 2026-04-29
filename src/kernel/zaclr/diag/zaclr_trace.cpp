#include <kernel/zaclr/include/zaclr_trace.h>

#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/host/zaclr_host.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

extern "C" const char* zaclr_trace_category_name(enum zaclr_trace_category category);

namespace
{
    static void zaclr_trace_write_dec(const struct zaclr_runtime* runtime, uint64_t value)
    {
        char buffer[21];
        size_t index = sizeof(buffer);

        if (runtime == NULL || runtime->state.host == NULL || runtime->state.host->write_text == NULL)
        {
            return;
        }

        buffer[--index] = '\0';
        if (value == 0u)
        {
            buffer[--index] = '0';
        }
        else
        {
            while (value != 0u && index > 0u)
            {
                buffer[--index] = (char)('0' + (value % 10u));
                value /= 10u;
            }
        }

        runtime->state.host->write_text(&buffer[index]);
    }
}

extern "C" void zaclr_trace_emit(const struct zaclr_runtime* runtime, const struct zaclr_trace_record* record)
{
    if (runtime == NULL || record == NULL || runtime->state.host == NULL || runtime->state.host->write_text == NULL)
    {
        return;
    }

    if (record->event_id == ZACLR_TRACE_EVENT_CALL_TARGET
        || record->event_id == ZACLR_TRACE_EVENT_METHOD_ENTER
        || record->event_id == ZACLR_TRACE_EVENT_METHOD_RETURN
        || record->event_id == ZACLR_TRACE_EVENT_METHOD_RVA
        || record->event_id == ZACLR_TRACE_EVENT_NATIVE_CALL
        || record->event_id == ZACLR_TRACE_EVENT_OPCODE_STEP
        || record->event_id == ZACLR_TRACE_EVENT_OPCODE_RAW
        || record->event_id == ZACLR_TRACE_EVENT_FRAME_PUSH
        || record->event_id == ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND)
    {
        return;
    }

    runtime->state.host->write_text("[ZACLR][");
    runtime->state.host->write_text(zaclr_trace_category_name(record->category));
    runtime->state.host->write_text("] ");
    if (record->message != NULL)
    {
        runtime->state.host->write_text(record->message);
    }
    if (runtime->state.host->write_hex64 != NULL)
    {
        runtime->state.host->write_text(" = 0x");
        runtime->state.host->write_hex64(record->value);
        runtime->state.host->write_text(" (");
        zaclr_trace_write_dec(runtime, record->value);
        runtime->state.host->write_text(")");
    }
    runtime->state.host->write_text("\n");
}

extern "C" void zaclr_trace_emit_text(const struct zaclr_runtime* runtime,
                                       enum zaclr_trace_category category,
                                       zaclr_trace_event_id event_id,
                                       const char* message)
{
    struct zaclr_trace_record record;
    record.category = category;
    record.event_id = event_id;
    record.message = message;
    record.value = 0u;
    zaclr_trace_emit(runtime, &record);
}

extern "C" void zaclr_trace_emit_value(const struct zaclr_runtime* runtime,
                                        enum zaclr_trace_category category,
                                        zaclr_trace_event_id event_id,
                                        const char* message,
                                        uint64_t value)
{
    struct zaclr_trace_record record;
    record.category = category;
    record.event_id = event_id;
    record.message = message;
    record.value = value;
    zaclr_trace_emit(runtime, &record);
}
