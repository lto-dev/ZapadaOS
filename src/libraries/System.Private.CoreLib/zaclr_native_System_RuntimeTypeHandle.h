#ifndef ZACLR_NATIVE_SYSTEM_RUNTIMETYPEHANDLE_H
#define ZACLR_NATIVE_SYSTEM_RUNTIMETYPEHANDLE_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_System_RuntimeTypeHandle
{
    static struct zaclr_result RuntimeTypeHandle_GetRuntimeTypeFromHandleSlow___STATIC__VOID__I__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack(struct zaclr_native_call_frame& frame);
    static struct zaclr_result ToIntPtr___STATIC__I__VALUETYPE(struct zaclr_native_call_frame& frame);
    static struct zaclr_result FromIntPtr___STATIC__VALUETYPE__I(struct zaclr_native_call_frame& frame);
    /* CoreCLR: RuntimeTypeHandle.GetAssemblyIfExists(RuntimeType type) -> RuntimeAssembly?
       FCALL (InternalCall), static, takes RuntimeType object, returns RuntimeAssembly or null.
       Reference: CLONES/runtime/src/coreclr/vm/runtimehandles.cpp:220 */
    static struct zaclr_result GetAssemblyIfExists___STATIC__CLASS_System_Reflection_RuntimeAssembly__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    /* CoreCLR: RuntimeTypeHandle.GetModuleIfExists(RuntimeType type) -> RuntimeModule?
       FCALL (InternalCall), static, takes RuntimeType object, returns RuntimeModule or null. */
    static struct zaclr_result GetModuleIfExists___STATIC__CLASS_System_Reflection_RuntimeModule__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetGCHandle___I__VALUETYPE_System_Runtime_InteropServices_GCHandleType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result FreeGCHandle___I__I(struct zaclr_native_call_frame& frame);
};

#endif
