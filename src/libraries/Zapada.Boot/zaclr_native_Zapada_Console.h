#ifndef ZACLR_NATIVE_ZAPADA_CONSOLE_H
#define ZACLR_NATIVE_ZAPADA_CONSOLE_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_Zapada_Console
{
    static struct zaclr_result Write___STATIC__VOID__STRING(struct zaclr_native_call_frame& frame);
    static struct zaclr_result WriteInt___STATIC__VOID__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result WriteHex___STATIC__VOID__I4(struct zaclr_native_call_frame& frame);
};

#endif
