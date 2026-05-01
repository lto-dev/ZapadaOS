#ifndef ZACLR_NATIVE_ZAPADA_FSVFS_RUNTIME_INTERNALCALLS_H
#define ZACLR_NATIVE_ZAPADA_FSVFS_RUNTIME_INTERNALCALLS_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_Zapada_Fs_Vfs_Runtime_InternalCalls
{
    static struct zaclr_result Publish___STATIC__I4__STRING__SZARRAY_U1(struct zaclr_native_call_frame& frame);
    static struct zaclr_result PublishBegin___STATIC__I4__STRING__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result PublishAppend___STATIC__I4__STRING__SZARRAY_U1__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result PublishEnd___STATIC__I4__STRING(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetTickCount___STATIC__I4(struct zaclr_native_call_frame& frame);
};

#endif
