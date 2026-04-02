#include <kernel/zaclr/process/zaclr_process.h>

extern "C" uint32_t zaclr_process_flags(const struct zaclr_process* process)
{
    return process != NULL ? process->flags : 0u;
}
