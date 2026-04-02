#ifndef ZACLR_NATIVE_SYSTEM_GC_H
#define ZACLR_NATIVE_SYSTEM_GC_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_System_GC
{
    static struct zaclr_result Collect___STATIC__VOID(struct zaclr_native_call_frame& frame);
    static struct zaclr_result _Collect___STATIC__VOID__I4__I4__BOOLEAN(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetMaxGeneration___STATIC__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result _CollectionCount___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetGenerationInternal___STATIC__I4__OBJECT(struct zaclr_native_call_frame& frame);
    static struct zaclr_result WaitForPendingFinalizers___STATIC__VOID(struct zaclr_native_call_frame& frame);
    static struct zaclr_result SuppressFinalizeInternal___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame);
    static struct zaclr_result _ReRegisterForFinalize___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetTotalMemory___STATIC__I8__BOOLEAN(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetFreeBytes___STATIC__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result Pin___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame);
    static struct zaclr_result Unpin___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame);
};

#endif
