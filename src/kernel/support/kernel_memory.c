#include <kernel/support/kernel_memory.h>

#include <kernel/mm/heap.h>

void* kernel_alloc(size_t size)
{
    return kheap_alloc(size);
}

void kernel_free(void* ptr)
{
    kheap_free(ptr);
}

size_t kernel_get_free_bytes(void)
{
    return kheap_get_free_bytes();
}

void kernel_memcpy(void* dst, const void* src, uint32_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    uint32_t i;

    for (i = 0u; i < n; ++i)
    {
        d[i] = s[i];
    }
}

void kernel_memmove(void* dst, const void* src, uint32_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;

    if (d == NULL || s == NULL || n == 0u || d == s)
    {
        return;
    }

    if (d < s || d >= (s + n))
    {
        kernel_memcpy(dst, src, n);
        return;
    }

    while (n != 0u)
    {
        --n;
        d[n] = s[n];
    }
}

void kernel_memset(void* dst, uint8_t val, uint32_t n)
{
    uint8_t* d = (uint8_t*)dst;
    uint32_t i;

    for (i = 0u; i < n; ++i)
    {
        d[i] = val;
    }
}

size_t kernel_strlen(const char* text)
{
    size_t length = 0u;

    if (text == NULL)
    {
        return 0u;
    }

    while (text[length] != '\0')
    {
        ++length;
    }

    return length;
}
