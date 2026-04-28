#ifndef ZACLR_NATIVE_SYSTEM_STRING_H
#define ZACLR_NATIVE_SYSTEM_STRING_H

#include <kernel/zaclr/heap/zaclr_array.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>
#include <kernel/zaclr/interop/zaclr_marshalling.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

struct zaclr_native_System_String
{
    static struct zaclr_result Concat___STATIC__STRING__STRING__STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result FastAllocateString___STATIC__STRING__PTR_VALUETYPE_System_Runtime_CompilerServices_MethodTable__I(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result get_Length___I4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result get_Chars___CHAR__I4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Substring___STRING__I4__I4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Substring___STRING__I4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result StartsWith___BOOLEAN__STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result StartsWith___BOOLEAN__STRING__VALUETYPE_System_StringComparison(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result EndsWith___BOOLEAN__STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Contains___BOOLEAN__STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Compare___STATIC__I4__STRING__STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result ToUpper___STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result ToLower___STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Trim___STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Replace___STRING__STRING__STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result IsNullOrEmpty___STATIC__BOOLEAN__STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result IndexOf___I4__CHAR(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result _ctor___VOID__SZARRAY_CHAR__I4__I4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result _ctor___VOID__CHAR__I4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result _ctor___VOID__SZARRAY_CHAR(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result get_Empty___STATIC__STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result op_Equality___STATIC__BOOLEAN__STRING__STRING(
        struct zaclr_native_call_frame& frame);
};

#endif
