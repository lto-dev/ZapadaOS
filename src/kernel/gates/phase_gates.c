#include <kernel/gates/phase_gates.h>
#include <kernel/gates/gate_pe_helpers.h>

#if !defined(ZACLR_ENABLED)
#include <kernel/clr/api/runtime_api.h>
#endif
#include <kernel/console.h>

void phase_gates_run(void)
{
    console_write("\n");
    console_write("========================================\n");
    console_write("Unified phase gates\n");
    console_write("========================================\n");
    console_write("[Gate] Kernel-ProcessSched : covered by subsystem bring-up in phase2b\n");
    console_write("[Gate] Kernel-IpcSyscall   : covered by subsystem bring-up in phase2b/phase2c\n");

    #if defined(ZACLR_ENABLED)
    console_write("[Gate] PE-Helpers       : skipped on ZACLR-selected build path\n");
    console_write("[Gate] ZACLR-Boot-Path   : selected by ENABLE_ZACLR\n");
    #else
    gate_pe_helpers_run();
    if (zapada_clr_runtime_initialize() == ZAPADA_CLR_RUNTIME_STATUS_NOT_IMPLEMENTED) {
        console_write("[Gate] Runtime-Api-Scaffold\n");
    }
    #endif
}
