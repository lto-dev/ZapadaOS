#include <kernel/zaclr/exec/zaclr_engine.h>
#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/heap/zaclr_array.h>
#include <kernel/zaclr/heap/zaclr_gc.h>
#include <kernel/zaclr/heap/zaclr_heap.h>
#include <kernel/zaclr/heap/zaclr_object.h>
#include <kernel/zaclr/heap/zaclr_string.h>
#include <kernel/zaclr/interop/zaclr_internal_call_contracts.h>
#include <kernel/zaclr/loader/zaclr_binder.h>
#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>

#include "zaclr_native_Zapada_Runtime_InternalCalls.h"

extern "C" {
#include <kernel/initramfs/ramdisk.h>
}

namespace
{
    static bool text_equals(const char* left, const char* right)
    {
        size_t index = 0u;

        if (left == NULL || right == NULL)
        {
            return false;
        }

        while (left[index] != '\0' && right[index] != '\0')
        {
            if (left[index] != right[index])
            {
                return false;
            }

            ++index;
        }

        return left[index] == right[index];
    }

    static void split_type_name(const char* full_type_name,
                                const char** out_namespace,
                                size_t* out_namespace_length,
                                const char** out_type_name)
    {
        const char* last_dot = NULL;
        const char* cursor;

        *out_namespace = full_type_name;
        *out_namespace_length = 0u;
        *out_type_name = full_type_name;

        if (full_type_name == NULL)
        {
            return;
        }

        for (cursor = full_type_name; *cursor != '\0'; ++cursor)
        {
            if (*cursor == '.')
            {
                last_dot = cursor;
            }
        }

        if (last_dot != NULL)
        {
            *out_namespace = full_type_name;
            *out_namespace_length = (size_t)(last_dot - full_type_name);
            *out_type_name = last_dot + 1;
        }
    }

    static bool type_matches(const struct zaclr_type_desc* type,
                             const char* type_namespace,
                             size_t type_namespace_length,
                             const char* type_name)
    {
        size_t index;

        if (type == NULL || type_name == NULL)
        {
            return false;
        }

        if (type->type_namespace.length != type_namespace_length || !text_equals(type->type_name.text, type_name))
        {
            return false;
        }

        for (index = 0u; index < type_namespace_length; ++index)
        {
            if (type->type_namespace.text[index] != type_namespace[index])
            {
                return false;
            }
        }

        return true;
    }

    static const struct zaclr_type_desc* find_type_by_name(const struct zaclr_loaded_assembly* assembly,
                                                           const char* full_type_name)
    {
        const char* type_namespace;
        size_t type_namespace_length;
        const char* type_name;
        uint32_t index;

        if (assembly == NULL || full_type_name == NULL)
        {
            return NULL;
        }

        split_type_name(full_type_name, &type_namespace, &type_namespace_length, &type_name);

        for (index = 0u; index < assembly->type_map.count; ++index)
        {
            const struct zaclr_type_desc* type = &assembly->type_map.types[index];
            if (type_matches(type, type_namespace, type_namespace_length, type_name))
            {
                return type;
            }
        }

        return NULL;
    }

    static const struct zaclr_method_desc* find_method_by_name(const struct zaclr_loaded_assembly* assembly,
                                                               const struct zaclr_type_desc* type,
                                                               const char* method_name)
    {
        uint32_t index;

        if (assembly == NULL || type == NULL || method_name == NULL)
        {
            return NULL;
        }

        for (index = 0u; index < assembly->method_map.count; ++index)
        {
            const struct zaclr_method_desc* method = &assembly->method_map.methods[index];
            if (method->owning_type_token.raw != type->token.raw)
            {
                continue;
            }

            if (text_equals(method->name.text, method_name))
            {
                return method;
            }
        }

        return NULL;
    }

    static struct zaclr_result load_assembly_from_ramdisk(struct zaclr_runtime* runtime,
                                                           const char* assembly_name,
                                                           const struct zaclr_loaded_assembly** out_assembly)
    {
        return zaclr_binder_load_assembly_by_name(runtime, assembly_name, out_assembly);
    }
}

struct zaclr_result zaclr_native_Zapada_Runtime_InternalCalls::RuntimeLoad___STATIC__I4__SZARRAY_U1(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_array_desc* dll_bytes;
    struct zaclr_slice image;
    struct zaclr_loaded_assembly loaded_assembly;
    struct zaclr_result status;

    if (frame.runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    status = zaclr_native_call_frame_arg_array(&frame, 0u, &dll_bytes);
    if (status.status != ZACLR_STATUS_OK)
    {
        return status;
    }

    if (dll_bytes == NULL || zaclr_array_element_size(dll_bytes) != 1u)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    image.data = (const uint8_t*)zaclr_array_data_const(dll_bytes);
    image.size = zaclr_array_length(dll_bytes);
    status = zaclr_loader_load_image(&frame.runtime->loader, &image, &loaded_assembly);
    if (status.status != ZACLR_STATUS_OK)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    status = zaclr_assembly_registry_register(&frame.runtime->assemblies, &loaded_assembly);
    if (status.status == ZACLR_STATUS_ALREADY_EXISTS)
    {
        const struct zaclr_loaded_assembly* assembly = zaclr_assembly_registry_find_by_name(&frame.runtime->assemblies,
                                                                                            loaded_assembly.assembly_name.text);
        zaclr_loader_release_loaded_assembly(&loaded_assembly);
        return zaclr_native_call_frame_set_i4(&frame, assembly != NULL ? (int32_t)assembly->id : -1);
    }

    if (status.status != ZACLR_STATUS_OK)
    {
        zaclr_loader_release_loaded_assembly(&loaded_assembly);
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)loaded_assembly.id);
}

struct zaclr_result zaclr_native_Zapada_Runtime_InternalCalls::RuntimeFindByName___STATIC__I4__STRING(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_loaded_assembly* assembly;
    const char* assembly_name;
    struct zaclr_result load_result;
    const struct zaclr_string_desc* lookup = NULL;

    if (frame.runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    load_result = zaclr_native_call_frame_arg_string(&frame, 0u, &lookup);
    if (load_result.status != ZACLR_STATUS_OK)
    {
        return load_result;
    }

    assembly_name = zaclr_string_chars(lookup);
    assembly = zaclr_assembly_registry_find_by_name(&frame.runtime->assemblies, assembly_name);
    if (assembly == NULL)
    {
        load_result = load_assembly_from_ramdisk(frame.runtime, assembly_name, &assembly);
        if (load_result.status != ZACLR_STATUS_OK && load_result.status != ZACLR_STATUS_NOT_FOUND)
        {
            return load_result;
        }
    }

    if (assembly == NULL)
    {
        return zaclr_native_call_frame_set_i4(&frame, -1);
    }

    return zaclr_native_call_frame_set_i4(&frame, (int32_t)assembly->id);
}

struct zaclr_result zaclr_native_Zapada_Runtime_InternalCalls::RuntimeCallMethod___STATIC__I4__STRING__STRING__I4(struct zaclr_native_call_frame& frame)
{
    const struct zaclr_loaded_assembly* assembly;
    const struct zaclr_type_desc* type;
    const struct zaclr_method_desc* method;
    const struct zaclr_string_desc* qualified_type_name_value = NULL;
    const struct zaclr_string_desc* method_name_value = NULL;
    const char* qualified_type_name;
    const char* method_name;
    struct zaclr_result execute_result;
    int32_t assembly_id;

    if (frame.runtime == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    execute_result = zaclr_native_call_frame_arg_string(&frame, 0u, &qualified_type_name_value);
    if (execute_result.status != ZACLR_STATUS_OK)
    {
        return execute_result;
    }
    execute_result = zaclr_native_call_frame_arg_string(&frame, 1u, &method_name_value);
    if (execute_result.status != ZACLR_STATUS_OK)
    {
        return execute_result;
    }
    execute_result = zaclr_native_call_frame_arg_i4(&frame, 2u, &assembly_id);
    if (execute_result.status != ZACLR_STATUS_OK)
    {
        return execute_result;
    }

    qualified_type_name = zaclr_string_chars(qualified_type_name_value);
    method_name = zaclr_string_chars(method_name_value);

    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "RuntimeCallMethod.AssemblyId",
                      (uint64_t)(uint32_t)assembly_id);
    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      qualified_type_name != NULL ? qualified_type_name : "<null-type>",
                      0u);
    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      method_name != NULL ? method_name : "<null-method>",
                      0u);

    assembly = zaclr_assembly_registry_find_by_id(&frame.runtime->assemblies, (uint32_t)assembly_id);
    if (assembly == NULL)
    {
        ZACLR_TRACE_VALUE(frame.runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "RuntimeCallMethod.AssemblyMissing",
                          (uint64_t)(uint32_t)assembly_id);
        return zaclr_native_call_frame_set_i4(&frame, 0);
    }

    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      assembly->assembly_name.text,
                      (uint64_t)assembly->id);

    type = find_type_by_name(assembly, qualified_type_name);
    if (type == NULL)
    {
        ZACLR_TRACE_VALUE(frame.runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "RuntimeCallMethod.TypeMissing",
                          0u);
        return zaclr_native_call_frame_set_i4(&frame, 0);
    }

    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      type->type_name.text,
                      (uint64_t)type->token.raw);

    method = find_method_by_name(assembly, type, method_name);
    if (method == NULL)
    {
        ZACLR_TRACE_VALUE(frame.runtime,
                          ZACLR_TRACE_CATEGORY_INTEROP,
                          ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                          "RuntimeCallMethod.MethodMissing",
                          0u);
        return zaclr_native_call_frame_set_i4(&frame, 0);
    }

    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      method->name.text,
                      (uint64_t)method->token.raw);

    execute_result = zaclr_engine_execute_method(&frame.runtime->engine,
                                                 frame.runtime,
                                                 &frame.runtime->boot_launch,
                                                 assembly,
                                                 method);

    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "RuntimeCallMethod.ExecuteStatus",
                      (uint64_t)execute_result.status);
    ZACLR_TRACE_VALUE(frame.runtime,
                      ZACLR_TRACE_CATEGORY_INTEROP,
                      ZACLR_TRACE_EVENT_INTERNAL_CALL_BIND,
                      "RuntimeCallMethod.ExecuteCategory",
                      (uint64_t)execute_result.category);

    zaclr_native_call_frame_set_i4(&frame, execute_result.status == ZACLR_STATUS_OK ? 1 : 0);
    return execute_result.status == ZACLR_STATUS_OK ? zaclr_result_ok() : execute_result;
}


