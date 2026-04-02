#include <kernel/zaclr/exec/zaclr_exceptions.h>

extern "C" uint32_t zaclr_exception_flags(const struct zaclr_exception_state* state)
{
    return state != NULL ? state->flags : 0u;
}
