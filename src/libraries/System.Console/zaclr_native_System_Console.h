#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>


extern "C" {
#include <kernel/console.h>
}

struct zaclr_native_System_Console
{
    static struct zaclr_result Write___STATIC__VOID__STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__I4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__STRING(
        struct zaclr_native_call_frame& frame);
};
