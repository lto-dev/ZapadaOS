/*
 * Zapada - src/libraries/Zapada.Fs.Fat32/zapada_fs_fat32_native_Zapada_Console.cpp
 */

#include "ZapadaFsFat32.h"

extern "C" void console_write(const char *str);
extern "C" void console_write_dec(uint64_t val);
extern "C" void console_write_hex64(uint64_t val);

HRESULT Library_zapada_fs_fat32_native_Zapada_Console::Write___STATIC__VOID__STRING(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();
    console_write(stack.Arg0().RecoverString());
    NANOCLR_NOCLEANUP_NOLABEL();
}

HRESULT Library_zapada_fs_fat32_native_Zapada_Console::WriteInt___STATIC__VOID__I4(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();
    console_write_dec((uint64_t)(uint32_t)stack.Arg0().NumericByRefConst().s4);
    NANOCLR_NOCLEANUP_NOLABEL();
}

HRESULT Library_zapada_fs_fat32_native_Zapada_Console::WriteHex___STATIC__VOID__I4(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();
    console_write_hex64((uint64_t)(uint32_t)stack.Arg0().NumericByRefConst().s4);
    NANOCLR_NOCLEANUP_NOLABEL();
}
