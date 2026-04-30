#ifndef ZACLR_NATIVE_ZAPADA_KERNEL_H
#define ZACLR_NATIVE_ZAPADA_KERNEL_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_Zapada_Kernel
{
    static struct zaclr_result SetBootPartLba___STATIC__VOID__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetTickCount___STATIC__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetBootCommandLine___STATIC__STRING(struct zaclr_native_call_frame& frame);
};

#endif
