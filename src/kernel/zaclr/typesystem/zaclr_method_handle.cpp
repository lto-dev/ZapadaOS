#include <kernel/zaclr/typesystem/zaclr_method_handle.h>

#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/metadata/zaclr_method_map.h>

extern "C" struct zaclr_result zaclr_method_handle_create(const struct zaclr_loaded_assembly* assembly,
                                                           const struct zaclr_method_desc* method,
                                                           struct zaclr_method_handle* out_handle)
{
    if (method == NULL || out_handle == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    out_handle->assembly = assembly;
    out_handle->method = method;
    out_handle->locator.assembly_id = assembly != NULL ? assembly->id : 0u;
    out_handle->locator.method_id = method->id;
    return zaclr_result_ok();
}

extern "C" uintptr_t zaclr_method_handle_pack(const struct zaclr_method_handle* handle)
{
    return handle != NULL && handle->method != NULL ? (uintptr_t)handle->method : (uintptr_t)0u;
}

extern "C" struct zaclr_result zaclr_method_handle_unpack(uintptr_t packed,
                                                           struct zaclr_method_handle* out_handle)
{
    const struct zaclr_method_desc* method = (const struct zaclr_method_desc*)packed;

    if (out_handle == NULL || method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
    }

    out_handle->assembly = NULL;
    out_handle->method = method;
    out_handle->locator.assembly_id = 0u;
    out_handle->locator.method_id = method->id;
    return zaclr_result_ok();
}
