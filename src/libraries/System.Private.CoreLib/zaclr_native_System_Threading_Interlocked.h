#ifndef ZACLR_NATIVE_SYSTEM_THREADING_INTERLOCKED_H
#define ZACLR_NATIVE_SYSTEM_THREADING_INTERLOCKED_H

#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>

struct zaclr_native_System_Threading_Interlocked
{
    static struct zaclr_result Exchange64___STATIC__I8__BYREF_I8__I8(struct zaclr_native_call_frame& frame);
    static struct zaclr_result ExchangeObject___STATIC__OBJECT__BYREF_OBJECT__OBJECT(struct zaclr_native_call_frame& frame);
};

#endif
