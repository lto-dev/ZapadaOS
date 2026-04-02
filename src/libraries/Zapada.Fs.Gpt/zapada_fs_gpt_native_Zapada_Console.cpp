/*
 * Zapada - src/libraries/Zapada.Fs.Gpt/zapada_fs_gpt_native_Zapada_Console.cpp
 */

#include "ZapadaFsGpt.h"

extern "C" void console_write(const char *str);

HRESULT Library_zapada_fs_gpt_native_Zapada_Console::Write___STATIC__VOID__STRING(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();
    console_write(stack.Arg0().RecoverString());
    NANOCLR_NOCLEANUP_NOLABEL();
}
