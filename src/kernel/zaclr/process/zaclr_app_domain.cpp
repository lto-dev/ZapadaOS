#include <kernel/zaclr/process/zaclr_app_domain.h>

extern "C" uint32_t zaclr_app_domain_flags(const struct zaclr_app_domain* domain)
{
    return domain != NULL ? domain->flags : 0u;
}
