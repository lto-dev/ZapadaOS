#include <kernel/zaclr/host/zaclr_host.h>

extern "C" const struct zaclr_host_vtable* zaclr_host_bind(const struct zaclr_host_vtable* host)
{
    return host;
}
