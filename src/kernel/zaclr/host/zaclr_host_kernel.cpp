#include <kernel/zaclr/host/zaclr_host.h>

extern "C" {
#include <kernel/console.h>
#include <kernel/panic.h>
#include <kernel/support/kernel_memory.h>
}

namespace {

void zaclr_host_kernel_write_text(const char* text)
{
    console_write(text != NULL ? text : "<null>");
}

void zaclr_host_kernel_write_hex64(uint64_t value)
{
    console_write_hex64(value);
}

void zaclr_host_kernel_panic(const char* file, int line, const char* message)
{
    kernel_panic(file, line, message);
}

uint64_t zaclr_host_kernel_monotonic_ticks(void)
{
    return 0u;
}

void* zaclr_host_kernel_alloc_pages(size_t size, uint32_t)
{
    return kernel_alloc(size);
}

void zaclr_host_kernel_free_pages(void* ptr, size_t)
{
    kernel_free(ptr);
}

const struct zaclr_host_vtable g_zaclr_host_kernel_vtable = {
    zaclr_host_kernel_write_text,
    zaclr_host_kernel_write_hex64,
    zaclr_host_kernel_panic,
    zaclr_host_kernel_monotonic_ticks,
    zaclr_host_kernel_alloc_pages,
    zaclr_host_kernel_free_pages
};

} /* namespace */

extern "C" const struct zaclr_host_vtable* zaclr_host_kernel_vtable(void)
{
    return &g_zaclr_host_kernel_vtable;
}
