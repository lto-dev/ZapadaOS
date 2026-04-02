#ifndef ZACLR_TRACE_H
#define ZACLR_TRACE_H

#include <kernel/zaclr/include/zaclr_status.h>
#include <kernel/zaclr/include/zaclr_trace_config.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_trace_record {
    enum zaclr_trace_category category;
    zaclr_trace_event_id event_id;
    const char* message;
    uint64_t value;
};

struct zaclr_runtime;

void zaclr_trace_emit(const struct zaclr_runtime* runtime, const struct zaclr_trace_record* record);
void zaclr_trace_emit_text(const struct zaclr_runtime* runtime,
                           enum zaclr_trace_category category,
                           zaclr_trace_event_id event_id,
                           const char* message);
void zaclr_trace_emit_value(const struct zaclr_runtime* runtime,
                            enum zaclr_trace_category category,
                            zaclr_trace_event_id event_id,
                            const char* message,
                            uint64_t value);

#define ZACLR_TRACE_TEXT(runtime, category, event_id, message) \
    zaclr_trace_emit_text((runtime), (category), (event_id), (message))

#define ZACLR_TRACE_VALUE(runtime, category, event_id, message, value) \
    zaclr_trace_emit_value((runtime), (category), (event_id), (message), (value))

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_TRACE_H */
