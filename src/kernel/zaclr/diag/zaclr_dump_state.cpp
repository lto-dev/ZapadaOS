#include <kernel/zaclr/diag/zaclr_dump_state.h>
#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/include/zaclr_trace.h>

extern "C" void zaclr_dump_runtime_state(const struct zaclr_runtime* runtime)
{
    ZACLR_TRACE_TEXT(runtime, ZACLR_TRACE_CATEGORY_DIAG, ZACLR_TRACE_EVENT_RUNTIME_INIT_END, "zaclr_dump_runtime_state");
}
