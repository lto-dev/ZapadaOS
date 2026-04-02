#ifndef ZACLR_NATIVE_ZAPADA_STORAGE_RAMDISK_H
#define ZACLR_NATIVE_ZAPADA_STORAGE_RAMDISK_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_Zapada_Storage_Ramdisk
{
    static struct zaclr_result FileCount___STATIC__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result Lookup___STATIC__I4__STRING(struct zaclr_native_call_frame& frame);
    static struct zaclr_result Read___STATIC__I4__I4__SZARRAY_U1__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetFileName___STATIC__STRING__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result GetFileSize___STATIC__I4__I4(struct zaclr_native_call_frame& frame);
};

#endif
