#include <kernel/zaclr/diag/zaclr_assert.h>
#include <kernel/zaclr/diag/zaclr_trace_events.h>

extern "C" void zaclr_assert_fail(const struct zaclr_runtime* runtime,
                                   const char* file,
                                   int,
                                   const char* expression)
{
    ZACLR_TRACE_TEXT(runtime, ZACLR_TRACE_CATEGORY_DIAG, ZACLR_TRACE_EVENT_ASSERT_FAILURE, file);
    ZACLR_TRACE_TEXT(runtime, ZACLR_TRACE_CATEGORY_DIAG, ZACLR_TRACE_EVENT_ASSERT_FAILURE, expression);
}
