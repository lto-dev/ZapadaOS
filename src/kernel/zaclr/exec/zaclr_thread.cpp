#include <kernel/zaclr/exec/zaclr_thread.h>

extern "C" uint32_t zaclr_exec_thread_flags(const struct zaclr_exec_thread* thread)
{
    return thread != NULL ? thread->flags : 0u;
}
