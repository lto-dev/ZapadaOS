#ifndef ZACLR_NATIVE_SYSTEM_THREADING_MONITOR_H
#define ZACLR_NATIVE_SYSTEM_THREADING_MONITOR_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_System_Threading_Monitor
{
    static struct zaclr_result TryEnter_FastPath___STATIC__BOOLEAN__OBJECT(struct zaclr_native_call_frame& frame);
    static struct zaclr_result TryEnter_FastPath_WithTimeout___STATIC__I4__OBJECT__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result Enter_Slowpath___STATIC__VOID__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack(struct zaclr_native_call_frame& frame);
    static struct zaclr_result TryEnter_Slowpath___STATIC__I4__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack__I4(struct zaclr_native_call_frame& frame);
};

#endif
