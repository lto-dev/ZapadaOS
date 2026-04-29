#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>


extern "C" {
#include <kernel/console.h>
}

struct zaclr_native_System_Console
{
    static struct zaclr_result WriteLine___STATIC__VOID(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__BOOLEAN(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__CHAR(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__SZARRAY_CHAR(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__SZARRAY_CHAR__I4__I4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__VALUETYPE_System_Decimal(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__R8(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__R4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__I4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__U4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__I8(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__U8(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__OBJECT(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__GENERICINST_VALUETYPE_System_ReadOnlySpanG1__1__CHAR(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__STRING__OBJECT(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__STRING__OBJECT__OBJECT(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__STRING__OBJECT__OBJECT__OBJECT(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__STRING__SZARRAY_OBJECT(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result WriteLine___STATIC__VOID__STRING__GENERICINST_VALUETYPE_System_ReadOnlySpanG1__1__OBJECT(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__STRING__OBJECT(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__STRING__OBJECT__OBJECT(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__STRING__OBJECT__OBJECT__OBJECT(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__STRING__SZARRAY_OBJECT(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__STRING__GENERICINST_VALUETYPE_System_ReadOnlySpanG1__1__OBJECT(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__BOOLEAN(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__CHAR(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__SZARRAY_CHAR(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__SZARRAY_CHAR__I4__I4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__R8(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__VALUETYPE_System_Decimal(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__R4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__I4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__U4(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__I8(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__U8(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__OBJECT(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__STRING(
        struct zaclr_native_call_frame& frame);

    static struct zaclr_result Write___STATIC__VOID__GENERICINST_VALUETYPE_System_ReadOnlySpanG1__1__CHAR(
        struct zaclr_native_call_frame& frame);
};
