#ifndef ZACLR_NATIVE_ZAPADA_SYSTEM_IPC_NATIVECALLS_H
#define ZACLR_NATIVE_ZAPADA_SYSTEM_IPC_NATIVECALLS_H

#include <kernel/zaclr/exec/zaclr_interop_dispatch.h>

struct zaclr_native_Zapada_System_Ipc_NativeCalls {
    static struct zaclr_result SysChannelCreate___STATIC__I4__STRING(struct zaclr_native_call_frame& frame);
    static struct zaclr_result SysChannelOpen___STATIC__I4__STRING(struct zaclr_native_call_frame& frame);
    static struct zaclr_result SysChannelSend___STATIC__I4__I4__SZARRAY_U1__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result SysChannelReceive___STATIC__I4__I4__SZARRAY_U1__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result SysChannelReply___STATIC__I4__I4__SZARRAY_U1__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result SysChannelClose___STATIC__I4__I4(struct zaclr_native_call_frame& frame);
    static struct zaclr_result SysChannelGetSenderPid___STATIC__I4__I4(struct zaclr_native_call_frame& frame);
};

#endif /* ZACLR_NATIVE_ZAPADA_SYSTEM_IPC_NATIVECALLS_H */
