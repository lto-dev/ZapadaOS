#ifndef ZACLR_NATIVE_SYSTEM_REFLECTION_METADATAIMPORT_H
#define ZACLR_NATIVE_SYSTEM_REFLECTION_METADATAIMPORT_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_System_Reflection_MetadataImport
{
    static struct zaclr_result GetMetadataImport___STATIC__I__CLASS_System_Reflection_RuntimeModule(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetName___STATIC__I4__I__I4__BYREF_PTR_U1(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetNamespace___STATIC__I4__I__I4__BYREF_PTR_U1(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetUserString___STATIC__I4__I__I4__BYREF_PTR_CHAR__BYREF_I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetDefaultValue___STATIC__I4__I__I4__BYREF_I8__BYREF_PTR_CHAR__BYREF_I4__BYREF_I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetEventProps___STATIC__I4__I__I4__BYREF_PTR_VOID__BYREF_I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetFieldDefProps___STATIC__I4__I__I4__BYREF_I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetPropertyProps___STATIC__I4__I__I4__BYREF_PTR_VOID__BYREF_I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetParentToken___STATIC__I4__I__I4__BYREF_I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetParamDefProps___STATIC__I4__I__I4__BYREF_I4__BYREF_I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetGenericParamProps___STATIC__I4__I__I4__BYREF_I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetScopeProps___STATIC__I4__I__BYREF_VALUETYPE_System_Guid(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetSigOfMethodDef___STATIC__I4__I__I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetSignatureFromToken___STATIC__I4__I__I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetMemberRefProps___STATIC__I4__I__I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetCustomAttributeProps___STATIC__I4__I__I4__BYREF_I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetClassLayout___STATIC__I4__I__I4__BYREF_I4__BYREF_I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetFieldOffset___STATIC__I4__I__I4__I4__BYREF_I4__BYREF_BOOLEAN(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetSigOfFieldDef___STATIC__I4__I__I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetFieldMarshal___STATIC__I4__I__I4__BYREF_VALUETYPE_System_Reflection_ConstArray(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetPInvokeMap___STATIC__I4__I__I4__BYREF_I4__BYREF_PTR_U1__BYREF_PTR_U1(struct zaclr_native_call_frame& frame);
    static struct zaclr_result IsValidToken___STATIC__BOOLEAN__I__I4(struct zaclr_native_call_frame& frame);
};

#endif
