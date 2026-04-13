#ifndef ZACLR_NATIVE_SYSTEM_RUNTIMETYPEHANDLE_H
#define ZACLR_NATIVE_SYSTEM_RUNTIMETYPEHANDLE_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_System_RuntimeTypeHandle
{
    static struct zaclr_result RuntimeTypeHandle_GetRuntimeTypeFromHandleSlow___STATIC__VOID__I__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack(struct zaclr_native_call_frame& frame);
    static struct zaclr_result ToIntPtr___STATIC__I__VALUETYPE(struct zaclr_native_call_frame& frame);
    static struct zaclr_result FromIntPtr___STATIC__VALUETYPE__I(struct zaclr_native_call_frame& frame);
};

#endif
