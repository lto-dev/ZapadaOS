#include <kernel/zaclr/runtime/zaclr_runtime_state.h>

extern "C" uint32_t zaclr_runtime_state_flags(const struct zaclr_runtime_state* state)
{
    return state != NULL ? state->flags : 0u;
}
