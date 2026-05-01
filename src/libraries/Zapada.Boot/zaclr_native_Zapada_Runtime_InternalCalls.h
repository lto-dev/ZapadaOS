#ifndef ZACLR_NATIVE_ZAPADA_RUNTIME_INTERNALCALLS_H
#define ZACLR_NATIVE_ZAPADA_RUNTIME_INTERNALCALLS_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_Zapada_Runtime_InternalCalls
{
    static struct zaclr_result RuntimeLoad___STATIC__I4__SZARRAY_U1(struct zaclr_native_call_frame& frame);
    static struct zaclr_result RuntimeTransitionToVfs___STATIC__I4__STRING(struct zaclr_native_call_frame& frame);
    static struct zaclr_result RuntimeBindFromSource___STATIC__I4__STRING(struct zaclr_native_call_frame& frame);
    static struct zaclr_result RuntimeCreateVfsLaunchState___STATIC__I4__STRING__STRING__STRING(struct zaclr_native_call_frame& frame);
};

#endif
