#include <kernel/zaclr/include/zaclr_trace.h>

extern "C" const char* zaclr_trace_category_name(enum zaclr_trace_category category)
{
    switch (category)
    {
        case ZACLR_TRACE_CATEGORY_BOOT: return "boot";
        case ZACLR_TRACE_CATEGORY_HOST: return "host";
        case ZACLR_TRACE_CATEGORY_RUNTIME: return "runtime";
        case ZACLR_TRACE_CATEGORY_PROCESS: return "process";
        case ZACLR_TRACE_CATEGORY_LOADER: return "loader";
        case ZACLR_TRACE_CATEGORY_METADATA: return "metadata";
        case ZACLR_TRACE_CATEGORY_EXEC: return "exec";
        case ZACLR_TRACE_CATEGORY_HEAP: return "heap";
        case ZACLR_TRACE_CATEGORY_INTEROP: return "interop";
        case ZACLR_TRACE_CATEGORY_DIAG: return "diag";
        case ZACLR_TRACE_CATEGORY_TEST: return "test";
        default: return "unknown";
    }
}
