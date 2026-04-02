#include <kernel/zaclr/interop/zaclr_internal_call_registry.h>
#include <kernel/zaclr/interop/zaclr_native_assembly.h>

#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/include/zaclr_trace.h>
#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/metadata/zaclr_method_map.h>
#include <kernel/zaclr/metadata/zaclr_type_map.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static void log_method_search_line(const char* label,
                                       const char* assembly_name,
                                       const char* type_namespace,
                                       const char* type_name,
                                       const char* method_name,
                                       uint64_t value)
    {
        console_write("[ZACLR][interop] ");
        console_write(label != NULL ? label : "MethodSearch");
        console_write(" assembly=");
        console_write(assembly_name != NULL ? assembly_name : "<null>");
        console_write(" type=");
        if (type_namespace != NULL && type_namespace[0] != '\0')
        {
            console_write(type_namespace);
            console_write(".");
        }
        console_write(type_name != NULL ? type_name : "<null>");
        console_write(" method=");
        console_write(method_name != NULL ? method_name : "<null>");
        console_write(" value=");
        console_write_dec(value);
        console_write("\n");
    }
}

extern "C" struct zaclr_result zaclr_register_generated_native_assemblies(struct zaclr_internal_call_registry* registry);

extern "C" bool zaclr_internal_call_text_equals(const char* left, const char* right)
{
    if (left == right)
    {
        return true;
    }

    if (left == NULL || right == NULL)
    {
        return false;
    }

    while (*left != '\0' && *right != '\0')
    {
        if (*left != *right)
        {
            return false;
        }

        ++left;
        ++right;
    }

    return *left == '\0' && *right == '\0';
}

extern "C" struct zaclr_result zaclr_internal_call_registry_initialize(struct zaclr_internal_call_registry* registry)
{
    uint32_t assembly_index;
    uint32_t cache_index;

    if (registry == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    registry->assembly_count = 0u;
    for (assembly_index = 0u; assembly_index < ZACLR_INTERNAL_CALL_REGISTRY_MAX_ASSEMBLIES; ++assembly_index)
    {
        registry->assemblies[assembly_index] = NULL;
    }

    registry->row_cache_next = 0u;
    for (cache_index = 0u; cache_index < ZACLR_INTERNAL_CALL_REGISTRY_ROW_CACHE_CAPACITY; ++cache_index)
    {
        registry->row_cache[cache_index].assembly_id = 0u;
        registry->row_cache[cache_index].method_row = 0u;
        registry->row_cache[cache_index].method = NULL;
    }

    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_internal_call_registry_register_assembly(
    struct zaclr_internal_call_registry* registry,
    const struct zaclr_native_assembly_descriptor* assembly)
{
    if (registry == NULL || assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    if (registry->assembly_count >= ZACLR_INTERNAL_CALL_REGISTRY_MAX_ASSEMBLIES)
    {
        return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    registry->assemblies[registry->assembly_count++] = assembly;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_internal_call_registry_register_generated(struct zaclr_internal_call_registry* registry)
{
    if (registry == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    return zaclr_register_generated_native_assemblies(registry);
}

extern "C" struct zaclr_result zaclr_internal_call_registry_find_assembly(
    const struct zaclr_internal_call_registry* registry,
    const char* assembly_name,
    const struct zaclr_native_assembly_descriptor** out_assembly)
{
    uint32_t assembly_index;

    if (registry == NULL || assembly_name == NULL || out_assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    for (assembly_index = 0u; assembly_index < registry->assembly_count; ++assembly_index)
    {
        const struct zaclr_native_assembly_descriptor* assembly = registry->assemblies[assembly_index];
        if (assembly == NULL || !zaclr_internal_call_text_equals(assembly->assembly_name, assembly_name))
        {
            continue;
        }

        *out_assembly = assembly;
        return zaclr_result_ok();
    }

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
}

extern "C" struct zaclr_result zaclr_internal_call_registry_resolve_exact(
    struct zaclr_internal_call_registry* registry,
    const struct zaclr_loaded_assembly* owning_assembly,
    const struct zaclr_type_desc* owning_type,
    const struct zaclr_method_desc* method,
    struct zaclr_internal_call_resolution* out_resolution)
{
    const struct zaclr_native_assembly_descriptor* assembly;
    uint32_t method_index;
    struct zaclr_result result;

    if (registry == NULL || owning_assembly == NULL || owning_type == NULL || method == NULL || out_resolution == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);
    }

    result = zaclr_internal_call_registry_find_assembly(registry,
                                                        owning_assembly->assembly_name.text,
                                                        &assembly);
    if (result.status != ZACLR_STATUS_OK)
    {
        log_method_search_line("MethodSearch.AssemblyNotFound",
                               owning_assembly->assembly_name.text,
                               owning_type->type_namespace.text,
                               owning_type->type_name.text,
                               method->name.text,
                               owning_assembly->id);
        return result;
    }

    log_method_search_line("MethodSearch.Searching",
                           owning_assembly->assembly_name.text,
                           owning_type->type_namespace.text,
                           owning_type->type_name.text,
                           method->name.text,
                           method->token.raw);

    out_resolution->assembly_name = NULL;
    out_resolution->method = NULL;

    for (method_index = 0u; method_index < ZACLR_INTERNAL_CALL_REGISTRY_ROW_CACHE_CAPACITY; ++method_index)
    {
        const struct zaclr_internal_call_row_cache_entry* cache_entry = &registry->row_cache[method_index];
        if (cache_entry->assembly_id == owning_assembly->id
            && cache_entry->method_row == method->token.raw
            && cache_entry->method != NULL
            && zaclr_native_bind_method_matches_managed(owning_assembly, owning_type, method, cache_entry->method))
        {
            log_method_search_line("MethodSearch.CacheHit",
                                   owning_assembly->assembly_name.text,
                                   owning_type->type_namespace.text,
                                   owning_type->type_name.text,
                                   method->name.text,
                                   cache_entry->method_row);
            out_resolution->assembly_name = assembly->assembly_name;
            out_resolution->method = cache_entry->method;
            return zaclr_result_ok();
        }
    }

    for (method_index = 0u; method_index < assembly->method_count; ++method_index)
    {
        const struct zaclr_native_bind_method* candidate = &assembly->method_lookup[method_index];
        log_method_search_line("MethodSearch.Check",
                               assembly->assembly_name,
                               candidate != NULL ? candidate->type_namespace : NULL,
                               candidate != NULL ? candidate->type_name : NULL,
                               candidate != NULL ? candidate->method_name : "<null>",
                               method_index + 1u);
        if (candidate != NULL
            && zaclr_native_bind_method_matches_managed(owning_assembly, owning_type, method, candidate))
        {
            log_method_search_line("MethodSearch.Found",
                                   assembly->assembly_name,
                                   candidate->type_namespace,
                                   candidate->type_name,
                                   candidate->method_name,
                                   method_index + 1u);
            out_resolution->assembly_name = assembly->assembly_name;
            out_resolution->method = candidate;
            registry->row_cache[registry->row_cache_next].assembly_id = owning_assembly->id;
            registry->row_cache[registry->row_cache_next].method_row = method->token.raw;
            registry->row_cache[registry->row_cache_next].method = candidate;
            registry->row_cache_next = (registry->row_cache_next + 1u) % ZACLR_INTERNAL_CALL_REGISTRY_ROW_CACHE_CAPACITY;
            return zaclr_result_ok();
        }
    }

    log_method_search_line("MethodSearch.NotFound",
                           owning_assembly->assembly_name.text,
                           owning_type->type_namespace.text,
                           owning_type->type_name.text,
                           method->name.text,
                           method->token.raw);

    return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_INTEROP);
}

