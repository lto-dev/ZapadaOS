/*
 * Zapada - src/libraries/Zapada.Storage/zapada_storage_native_Zapada_Console.cpp
 */

#include "ZapadaStorage.h"

extern "C" void console_write(const char *str);
extern "C" void console_write_dec(uint64_t val);
extern "C" void console_write_hex64(uint64_t val);

HRESULT Library_zapada_storage_native_Zapada_Console::Write___STATIC__VOID__STRING(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();
    const char *text = stack.Arg0().RecoverString();
    console_write(text != NULL ? text : "<null>");
    NANOCLR_NOCLEANUP_NOLABEL();
}

HRESULT Library_zapada_storage_native_Zapada_Console::WriteInt___STATIC__VOID__I4(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();
    console_write_dec((uint64_t)(uint32_t)stack.Arg0().NumericByRef().s4);
    NANOCLR_NOCLEANUP_NOLABEL();
}

HRESULT Library_zapada_storage_native_Zapada_Console::WriteHex___STATIC__VOID__I4(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();
    console_write_hex64((uint64_t)(uint32_t)stack.Arg0().NumericByRef().s4);
    NANOCLR_NOCLEANUP_NOLABEL();
}
