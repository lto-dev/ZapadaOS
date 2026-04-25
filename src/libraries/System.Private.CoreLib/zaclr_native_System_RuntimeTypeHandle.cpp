#include "zaclr_native_System_RuntimeTypeHandle.h"

#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/typesystem/zaclr_type_identity.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static struct zaclr_result load_runtime_type_handle_argument(struct zaclr_native_call_frame& frame,
                                                                 uint32_t index,
                                                                 const struct zaclr_runtime_type_desc** out_type,
                                                                 zaclr_object_handle* out_handle)
    {
        struct zaclr_type_identity identity = {};
        struct zaclr_result result;

        if (out_type == NULL || out_handle == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_type = NULL;
        *out_handle = 0u;

        result = zaclr_native_call_frame_arg_object(&frame, index, out_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (*out_handle == 0u)
        {
            return zaclr_result_ok();
        }

        result = zaclr_type_identity_from_runtime_type_handle(frame.runtime, *out_handle, &identity);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }
        zaclr_type_identity_reset(&identity);

        *out_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, *out_handle);
        return *out_type != NULL
            ? zaclr_result_ok()
            : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    static struct zaclr_result load_runtime_type_from_i8(struct zaclr_native_call_frame& frame,
                                                         uint32_t index,
                                                         const struct zaclr_runtime_type_desc** out_type,
                                                         zaclr_object_handle* out_handle)
    {
        int64_t raw_handle = 0;
        struct zaclr_result result;

        if (out_type == NULL || out_handle == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
        }

        *out_type = NULL;
        *out_handle = 0u;

        result = zaclr_native_call_frame_arg_i8(&frame, index, &raw_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (raw_handle == 0)
        {
            return zaclr_result_ok();
        }

        result = zaclr_runtime_type_find_by_native_handle(frame.runtime,
                                                          (uintptr_t)raw_handle,
                                                          out_handle);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_type = zaclr_runtime_type_from_handle_const(&frame.runtime->heap, *out_handle);
        return *out_type != NULL
            ? zaclr_result_ok()
            : zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
    }
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::RuntimeTypeHandle_GetRuntimeTypeFromHandleSlow___STATIC__VOID__I__VALUETYPE_System_Runtime_CompilerServices_ObjectHandleOnStack(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_from_i8(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_store_byref_object(&frame, 1u, runtime_type_handle);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::ToIntPtr___STATIC__I__VALUETYPE(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_i8(&frame,
                                          runtime_type != NULL
                                              ? (int64_t)runtime_type->native_type_handle
                                              : 0);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::FromIntPtr___STATIC__VALUETYPE__I(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    struct zaclr_result result = load_runtime_type_from_i8(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_object(&frame, runtime_type_handle);
}

/* CoreCLR: RuntimeTypeHandle.GetAssemblyIfExists(RuntimeType type) -> RuntimeAssembly?
   FCALL (InternalCall), static, takes RuntimeType object, returns RuntimeAssembly or null.
   Reference: CLONES/runtime/src/coreclr/vm/runtimehandles.cpp:220-231
   
   In CoreCLR this extracts the MethodTable from the RuntimeType, gets the
   Assembly* from the MethodTable's Module, then returns the Assembly's
   managed "exposed object" (RuntimeAssembly) if it has already been created.
   
   In ZACLR, the RuntimeType stores its type_assembly directly.  We lazily
   create the managed RuntimeAssembly via zaclr_runtime_assembly_get_or_create. */
struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetAssemblyIfExists___STATIC__CLASS_System_Reflection_RuntimeAssembly__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    zaclr_object_handle assembly_handle = 0u;
    struct zaclr_result result;

    /* Argument 0: RuntimeType type */
    result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (runtime_type == NULL || runtime_type->type_assembly == NULL)
    {
        /* No type or no assembly -> return null (caller falls back to GetAssemblySlow) */
        return zaclr_native_call_frame_set_object(&frame, 0u);
    }

    /* Get or create the managed RuntimeAssembly for this assembly */
    result = zaclr_runtime_assembly_get_or_create(&frame.runtime->heap,
                                                   (struct zaclr_loaded_assembly*)runtime_type->type_assembly,
                                                   &assembly_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        /* If allocation fails, return null to let caller try the slow path */
        return zaclr_native_call_frame_set_object(&frame, 0u);
    }

    return zaclr_native_call_frame_set_object(&frame, assembly_handle);
}

/* CoreCLR: RuntimeTypeHandle.GetModuleIfExists(RuntimeType type) -> RuntimeModule?
   The immediate caller falls back to QCall GetModuleSlow if this returns null.
   ZACLR currently has no managed RuntimeModule object surface, but GetModule() only
   needs a module whose RuntimeModule.get_RuntimeType() returns the declaring type's
   RuntimeType. Returning the existing RuntimeAssembly object preserves forward motion
   for the current interpreter slice because both RuntimeAssembly and RuntimeModule are
   managed wrappers around the owning native assembly/module concept. */
struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetModuleIfExists___STATIC__CLASS_System_Reflection_RuntimeModule__CLASS_System_RuntimeType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    zaclr_object_handle assembly_handle = 0u;
    struct zaclr_result result;

    console_write("[ZACLR][rtth] compiled GetModuleIfExists reached\n");

    result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    if (runtime_type == NULL || runtime_type->type_assembly == NULL)
    {
        return zaclr_native_call_frame_set_object(&frame, 0u);
    }

    result = zaclr_runtime_assembly_get_or_create(&frame.runtime->heap,
                                                   (struct zaclr_loaded_assembly*)runtime_type->type_assembly,
                                                   &assembly_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_object(&frame, 0u);
    }

    return zaclr_native_call_frame_set_object(&frame, assembly_handle);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::GetGCHandle___I__VALUETYPE_System_Runtime_InteropServices_GCHandleType(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle runtime_type_handle = 0u;
    const struct zaclr_runtime_type_desc* runtime_type = NULL;
    int32_t handle_kind = 0;
    uint32_t index = 0u;
    struct zaclr_handle_table* table;
    struct zaclr_result result;

    result = load_runtime_type_handle_argument(frame, 0u, &runtime_type, &runtime_type_handle);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_native_call_frame_arg_i4(&frame, 1u, &handle_kind);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    table = frame.runtime != NULL ? &frame.runtime->boot_launch.handle_table : NULL;
    if (table == NULL || runtime_type_handle == 0u)
    {
        return zaclr_native_call_frame_set_i8(&frame, 0);
    }

    result = zaclr_handle_table_store_ex(table, runtime_type_handle, (uint32_t)handle_kind, &index);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_i8(&frame, (int64_t)(uintptr_t)&table->entries[index].handle);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::FreeGCHandle___I__I(struct zaclr_native_call_frame& frame)
{
    int64_t handle_value = 0;
    struct zaclr_result result = zaclr_native_call_frame_arg_i8(&frame, 0u, &handle_value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_i8(&frame, handle_value);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::get_Module___CLASS_System_Reflection_Module(struct zaclr_native_call_frame& frame)
{
    zaclr_object_handle value = 0u;
    struct zaclr_result result = zaclr_native_call_frame_arg_object(&frame, 0u, &value);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return zaclr_native_call_frame_set_object(&frame, value);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::get_ModuleHandle___VALUETYPE_System_ModuleHandle(struct zaclr_native_call_frame& frame)
{
    return zaclr_native_call_frame_set_i8(&frame, 0);
}

struct zaclr_result zaclr_native_System_RuntimeTypeHandle::Equals___BOOLEAN__VALUETYPE_System_ModuleHandle(struct zaclr_native_call_frame& frame)
{
    (void)frame;
    return zaclr_native_call_frame_set_bool(&frame, true);
}
