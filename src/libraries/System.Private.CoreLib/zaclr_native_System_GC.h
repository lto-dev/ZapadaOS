#ifndef ZACLR_NATIVE_SYSTEM_GC_H
#define ZACLR_NATIVE_SYSTEM_GC_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_System_GC
{
    static struct zaclr_result Collect___STATIC__VOID(struct zaclr_native_call_frame& frame);
    static struct zaclr_result _Collect___STATIC__VOID__I4__I4__BOOLEAN(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_Collect___STATIC__VOID__I4__I4__BOOLEAN(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetMaxGeneration___STATIC__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result _CollectionCount___STATIC__I4__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetGenerationInternal___STATIC__I4__OBJECT(struct zaclr_native_call_frame& frame);
    static struct zaclr_result WaitForPendingFinalizers___STATIC__VOID(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_WaitForPendingFinalizers___STATIC__VOID(struct zaclr_native_call_frame& frame);
    static struct zaclr_result SuppressFinalizeInternal___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame);
    static struct zaclr_result _ReRegisterForFinalize___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_ReRegisterForFinalize___STATIC__VOID__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetTotalMemory___STATIC__I8__BOOLEAN(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetFreeBytes___STATIC__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetMemoryInfo___STATIC__VOID__I4__I4__I4__I8__I8__I8__I8__I8__I8__I8__I8__I8__I4__I8__I8(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetSegmentSize___STATIC__I8(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetLastGCPercentTimeInGC___STATIC__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetGenerationSize___STATIC__I8__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetAllocatedBytesForCurrentThread___STATIC__I8(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetTotalAllocatedBytesApproximate___STATIC__I8(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetMemoryLoad___STATIC__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result _RegisterForFullGCNotification___STATIC__BOOLEAN__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result _CancelFullGCNotification___STATIC__BOOLEAN(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_GetNextFinalizableObject___STATIC__OBJECT(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_AddMemoryPressure___STATIC__VOID__I8(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_RemoveMemoryPressure___STATIC__VOID__I8(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_StartNoGCRegion___STATIC__I4__I8__BOOLEAN__I8__I8(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_EndNoGCRegion___STATIC__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_RegisterFrozenSegment___STATIC__I__I__I(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_UnregisterFrozenSegment___STATIC__VOID__I(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_AllocateNewArray___STATIC__OBJECT__I__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_GetTotalAllocatedBytesPrecise___STATIC__I8(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_GetTotalMemory___STATIC__I8__BOOLEAN(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_WaitForFullGCApproach___STATIC__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GCInterface_WaitForFullGCComplete___STATIC__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result Pin___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame);
    static struct zaclr_result Unpin___STATIC__VOID__OBJECT(struct zaclr_native_call_frame& frame);
};

#endif
