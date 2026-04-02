#include <kernel/zaclr/process/zaclr_security_context.h>

extern "C" uint32_t zaclr_security_context_flags(const struct zaclr_security_context* context)
{
    return context != NULL ? context->flags : 0u;
}
