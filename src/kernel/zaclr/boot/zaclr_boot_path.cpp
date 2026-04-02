#include <kernel/zaclr/boot/zaclr_boot_path.h>

#include <kernel/zaclr/boot/zaclr_boot.h>
#include <kernel/zaclr/host/zaclr_host.h>

extern "C" {
#include <kernel/console.h>
}

namespace {

struct zaclr_boot_launch_target
{
    const char* image_path;
    const char* entry_type;
    const char* entry_method;
    const char* label;
};

static const struct zaclr_boot_launch_target g_conformance_launch_target = {
    "Zapada.Conformance.dll",
    "Zapada.Conformance.DllMain",
    "Initialize",
    "[ZACLR] Launching primary conformance target Zapada.Conformance.dll -> Zapada.Conformance.DllMain.Initialize\n"
};

static const struct zaclr_boot_launch_target g_boot_launch_target = {
    "Zapada.Boot.dll",
    "Zapada.Boot.Program",
    "Main",
    "[ZACLR] Preserved integration target Zapada.Boot.dll -> Zapada.Boot.Program.Main\n"
};

void zaclr_boot_write_status(const char* label, const struct zaclr_result& result)
{
    console_write(label);
    console_write(" status=");
    console_write_dec((uint64_t)result.status);
    console_write(" category=");
    console_write_dec((uint64_t)result.category);
    console_write("\n");
}

} /* namespace */

extern "C" zaclr_kernel_boot_status_t zaclr_boot_kernel_entry(void)
{
    static struct zaclr_runtime runtime;
    struct zaclr_bootstrap_contract bootstrap = {};
    struct zaclr_runtime_config config = {};
    struct zaclr_launch_request request = {};
    struct zaclr_result result;
    zaclr_process_id process_id = 0u;

    console_write("[ZACLR] Boot path selected\n");

    bootstrap.host = zaclr_host_kernel_vtable();
    bootstrap.trace = zaclr_trace_config_default();

    config.trace = zaclr_trace_config_default();
    config.enable_metadata_validation = true;
    config.enable_opcode_trace = true;
    config.enable_heap_trace = false;

    result = zaclr_bootstrap_entry(&runtime, &bootstrap, &config);
    if (result.status != ZACLR_STATUS_OK)
    {
        zaclr_boot_write_status("[ZACLR] Bootstrap failed", result);
        return ZACLR_KERNEL_BOOT_STATUS_BOOTSTRAP_FAILED;
    }

    console_write("[ZACLR] Runtime initialized\n");

    request.image_path = g_boot_launch_target.image_path;
    request.entry_type = g_boot_launch_target.entry_type;
    request.entry_method = g_boot_launch_target.entry_method;
    request.user = 0u;
    request.group = 0u;
    request.flags = 0u;

    console_write("[ZACLR] Primary runtime milestone: bootloader-first execution\n");
    console_write(g_boot_launch_target.label);
    console_write(g_conformance_launch_target.label);

    result = zaclr_runtime_launch(&runtime, &request, &process_id);
    if (result.status != ZACLR_STATUS_OK)
    {
        zaclr_boot_write_status("[ZACLR] Launch failed", result);
        return ZACLR_KERNEL_BOOT_STATUS_LAUNCH_FAILED;
    }

    console_write("[ZACLR] Launch completed process=");
    console_write_dec((uint64_t)process_id);
    console_write("\n");
    console_write("[ZACLR] Bootloader execution reached ZACLR-owned runtime path\n");
    return ZACLR_KERNEL_BOOT_STATUS_OK;
}
