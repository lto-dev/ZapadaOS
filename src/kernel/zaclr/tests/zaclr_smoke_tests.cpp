#include <kernel/zaclr/tests/zaclr_smoke_tests.h>

#include <kernel/zaclr/host/zaclr_host.h>
#include <kernel/zaclr/interop/zaclr_marshalling.h>

extern "C" {
#include <kernel/initramfs/ramdisk.h>
}

namespace
{
    static struct zaclr_result expect(bool condition)
    {
        return condition ? zaclr_result_ok()
                         : zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_DIAG);
    }

    static struct zaclr_result run_launch_smoke_test(struct zaclr_runtime* runtime)
    {
        ramdisk_file_t file;
        struct zaclr_launch_request request = {};
        struct zaclr_marshaled_assembly_lookup_argument lookup_argument = { "Zapada.Boot" };
        struct zaclr_marshaled_assembly_lookup_result lookup_result = {};
        struct zaclr_marshaled_tick_count_result tick_result = {};
        zaclr_process_id process_id = 0u;
        struct zaclr_result result;

        if (runtime == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_DIAG);
        }

        if (ramdisk_lookup("Zapada.Boot.dll", &file) != RAMDISK_OK)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_DIAG);
        }

        request.image_path = "Zapada.Boot.dll";
        request.entry_type = "Zapada.Boot.Program";
        request.entry_method = "Main";
        request.user = 0u;
        request.group = 0u;
        request.flags = 0u;

        result = zaclr_runtime_launch(runtime, &request, &process_id);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = expect(process_id == runtime->state.boot_process_id && process_id != 0u);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = expect(runtime->state.boot_assembly_id != 0u && runtime->state.boot_entry_method_id != 0u);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = expect((runtime->state.flags & ZACLR_RUNTIME_STATE_FLAG_BOOT_COMPLETED) != 0u
                     && runtime->state.boot_completed_method_id == runtime->state.boot_entry_method_id);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_marshal_internal_call(runtime,
                                             102u,
                                             &lookup_argument,
                                             sizeof(lookup_argument),
                                             &lookup_result,
                                             sizeof(lookup_result));
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = expect(lookup_result.assembly_id == runtime->state.boot_assembly_id);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_marshal_internal_call(runtime,
                                             101u,
                                             NULL,
                                             0u,
                                             &tick_result,
                                             sizeof(tick_result));
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        return expect(tick_result.value == 0u);
    }
}

extern "C" struct zaclr_result zaclr_run_smoke_tests(struct zaclr_runtime* runtime)
{
    struct zaclr_runtime local_runtime;
    struct zaclr_bootstrap_contract bootstrap = {};
    struct zaclr_runtime_config config = {};
    struct zaclr_result result;

    if (runtime == NULL)
    {
        runtime = &local_runtime;
        bootstrap.host = zaclr_host_kernel_vtable();
        bootstrap.trace = zaclr_trace_config_default();
        config.trace = zaclr_trace_config_default();
        config.enable_metadata_validation = true;
        config.enable_opcode_trace = false;
        config.enable_heap_trace = false;

        result = zaclr_runtime_initialize(runtime, &bootstrap, &config);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
    }

    return run_launch_smoke_test(runtime);
}
