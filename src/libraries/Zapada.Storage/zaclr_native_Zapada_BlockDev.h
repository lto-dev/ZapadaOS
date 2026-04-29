#ifndef ZACLR_NATIVE_ZAPADA_BLOCKDEV_H
#define ZACLR_NATIVE_ZAPADA_BLOCKDEV_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_Zapada_BlockDev
{
    static struct zaclr_result SectorCount___STATIC__I8(struct zaclr_native_call_frame& frame);
    static struct zaclr_result ReadSector___STATIC__I4__I8__I4__SZARRAY_I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result WriteSector___STATIC__I4__I8__I4__SZARRAY_I4(struct zaclr_native_call_frame& frame);
};

#endif
