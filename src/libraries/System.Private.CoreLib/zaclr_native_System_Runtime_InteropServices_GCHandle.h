#ifndef ZACLR_NATIVE_SYSTEM_RUNTIME_INTEROPSERVICES_GCHANDLE_H
#define ZACLR_NATIVE_SYSTEM_RUNTIME_INTEROPSERVICES_GCHANDLE_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_System_Runtime_InteropServices_GCHandle
{
    static struct zaclr_result _InternalAlloc___STATIC__I__OBJECT__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result _InternalFree___STATIC__BOOLEAN__I(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCHandle_InternalFreeWithGCTransition___STATIC__VOID__I(struct zaclr_native_call_frame& frame);
    static struct zaclr_result InternalGet___STATIC__OBJECT__I(struct zaclr_native_call_frame& frame);
    static struct zaclr_result InternalSet___STATIC__VOID__I__OBJECT(struct zaclr_native_call_frame& frame);
    static struct zaclr_result InternalCompareExchange___STATIC__OBJECT__I__OBJECT__OBJECT(struct zaclr_native_call_frame& frame);
};

#endif
