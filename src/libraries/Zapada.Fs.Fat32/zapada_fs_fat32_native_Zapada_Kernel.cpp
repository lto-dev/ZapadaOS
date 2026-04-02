/*
 * Zapada - src/libraries/Zapada.Fs.Fat32/zapada_fs_fat32_native_Zapada_Kernel.cpp
 */

#include "ZapadaFsFat32.h"

extern "C" int32_t zapada_boot_native_shared_get_boot_part_lba(void);

HRESULT Library_zapada_fs_fat32_native_Zapada_Kernel::GetBootPartLba___STATIC__I4(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();
    stack.SetResult_I4(zapada_boot_native_shared_get_boot_part_lba());
    NANOCLR_NOCLEANUP_NOLABEL();
}
