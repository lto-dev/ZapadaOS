#ifndef ZACLR_NATIVE_ZAPADA_CONFORMANCE_RUNTIME_INTERNALCALLS_H
#define ZACLR_NATIVE_ZAPADA_CONFORMANCE_RUNTIME_INTERNALCALLS_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_Zapada_Conformance_Runtime_InternalCalls
{
    static struct zaclr_result Write___STATIC__VOID__STRING(struct zaclr_native_call_frame& frame);
    static struct zaclr_result WriteInt___STATIC__VOID__I4(struct zaclr_native_call_frame& frame);
    
    static struct zaclr_result GcCollect___STATIC__VOID(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GcGetTotalMemory___STATIC__I8__BOOLEAN(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GcGetFreeBytes___STATIC__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GcPin___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GcUnpin___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame);
};

#endif
