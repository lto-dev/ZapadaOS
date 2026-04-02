/*
 * Zapada - src/libraries/Zapada.Fs.Fat32/zapada_fs_fat32_native_Zapada_BlockDev.cpp
 */

#include "ZapadaFsFat32.h"

extern "C" int32_t native_read_sector(int64_t lba, int32_t count, void *arr_obj);

HRESULT Library_zapada_fs_fat32_native_Zapada_BlockDev::ReadSector___STATIC__I4__I8__I4__SZARRAY_I4(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();
    CLR_INT64 lba = stack.Arg0().NumericByRefConst().s8;
    CLR_INT32 count = stack.Arg1().NumericByRefConst().s4;
    CLR_RT_HeapBlock_Array *buffer = stack.Arg2().DereferenceArray();
    stack.SetResult_I4(native_read_sector((int64_t)lba, (int32_t)count, buffer));
    NANOCLR_NOCLEANUP_NOLABEL();
}
