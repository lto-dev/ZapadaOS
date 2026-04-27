#ifndef ZACLR_NATIVE_SYSTEM_RUNTIMETYPEHANDLE_H
#define ZACLR_NATIVE_SYSTEM_RUNTIMETYPEHANDLE_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_System_RuntimeTypeHandle
{
    static struct zaclr_result RuntimeTypeHandle_GetRuntimeTypeFromHandleSlow___STATIC__VOID__I__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack(struct zaclr_native_call_frame& frame);
    static struct zaclr_result ToIntPtr___STATIC__I__VALUETYPE(struct zaclr_native_call_frame& frame);
    static struct zaclr_result FromIntPtr___STATIC__VALUETYPE__I(struct zaclr_native_call_frame& frame);
    static struct zaclr_result InternalAllocNoChecks_FastPath___STATIC__OBJECT__PTR_VALUETYPE_System_Runtime_CompilerServices_MethodTable(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetToken___STATIC__I4__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    /* CoreCLR: RuntimeTypeHandle.GetAssemblyIfExists(RuntimeType type) -> RuntimeAssembly?
       FCALL (InternalCall), static, takes RuntimeType object, returns RuntimeAssembly or null.
       Reference: CLONES/runtime/src/coreclr/vm/runtimehandles.cpp:220 */
    static struct zaclr_result GetAssemblyIfExists___STATIC__CLASS_System_Reflection_RuntimeAssembly__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    /* CoreCLR: RuntimeTypeHandle.GetModuleIfExists(RuntimeType type) -> RuntimeModule?
       FCALL (InternalCall), static, takes RuntimeType object, returns RuntimeModule or null. */
    static struct zaclr_result GetModuleIfExists___STATIC__CLASS_System_Reflection_RuntimeModule__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetAttributes___STATIC__VALUETYPE_System_Reflection_TypeAttributes__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetElementTypeHandle___STATIC__I__I(struct zaclr_native_call_frame& frame);
    static struct zaclr_result CompareCanonicalHandles___STATIC__BOOLEAN__CLASS_System_RuntimeType__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetArrayRank___STATIC__I4__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result IsUnmanagedFunctionPointer___STATIC__BOOLEAN__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetFirstIntroducedMethod___STATIC__VALUETYPE_System_RuntimeMethodHandleInternal__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetNextIntroducedMethod___STATIC__VOID__BYREF_VALUETYPE_System_RuntimeMethodHandleInternal(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetNumVirtuals___STATIC__I4__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetUtf8NameInternal___STATIC__PTR_U1__PTR_VALUETYPE_System_Runtime_CompilerServices_MethodTable(struct zaclr_native_call_frame& frame);
    static struct zaclr_result IsGenericVariable___STATIC__BOOLEAN__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetGenericVariableIndex___STATIC__I4__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result ContainsGenericVariables___STATIC__BOOLEAN__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetGCHandle___I__VALUETYPE_System_Runtime_InteropServices_GCHandleType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result FreeGCHandle___I__I(struct zaclr_native_call_frame& frame);
    static struct zaclr_result QCall_GetGCHandleForTypeHandle(struct zaclr_native_call_frame& frame);
    static struct zaclr_result QCall_FreeGCHandleForTypeHandle(struct zaclr_native_call_frame& frame);
    static struct zaclr_result QCall_TypeHandle_GetCorElementType(struct zaclr_native_call_frame& frame);
    static struct zaclr_result QCall_RuntimeTypeHandle_GetDeclaringTypeHandle(struct zaclr_native_call_frame& frame);
    static struct zaclr_result QCall_RuntimeTypeHandle_ConstructName(struct zaclr_native_call_frame& frame);
    static struct zaclr_result get_Module___CLASS_System_Reflection_Module(struct zaclr_native_call_frame& frame);
    static struct zaclr_result get_ModuleHandle___VALUETYPE_System_ModuleHandle(struct zaclr_native_call_frame& frame);
    static struct zaclr_result Equals___BOOLEAN__VALUETYPE_System_ModuleHandle(struct zaclr_native_call_frame& frame);
};

#endif
