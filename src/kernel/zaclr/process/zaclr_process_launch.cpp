#include <kernel/zaclr/process/zaclr_process_launch.h>

extern "C" {
#include <kernel/console.h>
#include <kernel/serial.h>
}

namespace
{
    static void zaclr_debug_write_text(const char* text)
    {
        if (text != NULL)
        {
            console_write(text);
        }
    }

    static void zaclr_debug_write_name_view(struct zaclr_name_view view)
    {
        size_t index;

        if (view.text == NULL)
        {
            console_write("<null>");
            return;
        }

        for (index = 0u; index < view.length; ++index)
        {
            serial_write_char(view.text[index]);
        }
    }

    static void zaclr_debug_log_launch_type_resolution(const struct zaclr_loaded_assembly* assembly,
                                                       const char* requested_type,
                                                       const char* requested_namespace,
                                                       const char* requested_name)
    {
        uint32_t type_index;

        console_write("[ZACLR][process] launch type resolve request=");
        zaclr_debug_write_text(requested_type);
        console_write(" namespace=");
        zaclr_debug_write_text(requested_namespace);
        console_write(" type=");
        zaclr_debug_write_text(requested_name);
        console_write("\n");

        if (assembly == NULL)
        {
            return;
        }

        for (type_index = 0u; type_index < assembly->type_map.count; ++type_index)
        {
            const struct zaclr_type_desc* type = &assembly->type_map.types[type_index];
            console_write("[ZACLR][process] assembly type namespace=");
            zaclr_debug_write_name_view(type->type_namespace);
            console_write(" type=");
            zaclr_debug_write_name_view(type->type_name);
            console_write("\n");
        }
    }

    static bool zaclr_name_view_equals(struct zaclr_name_view view, const char* text)
    {
        size_t index;

        if (view.text == NULL || text == NULL)
        {
            return false;
        }

        for (index = 0u; index < view.length; ++index)
        {
            if (text[index] == '\0' || view.text[index] != text[index])
            {
                return false;
            }
        }

        return text[view.length] == '\0';
    }

    static void zaclr_split_type_name(const char* full_name,
                                      const char** out_namespace,
                                      size_t* out_namespace_length,
                                      const char** out_type_name)
    {
        const char* cursor = full_name;
        const char* separator = NULL;

        while (cursor != NULL && *cursor != '\0')
        {
            if (*cursor == '.')
            {
                separator = cursor;
            }

            cursor++;
        }

        if (separator == NULL)
        {
            *out_namespace = "";
            *out_namespace_length = 0u;
            *out_type_name = full_name;
            return;
        }

        *out_namespace = full_name;
        *out_namespace_length = (size_t)(separator - full_name);
        *out_type_name = separator + 1;
    }

    static bool zaclr_type_matches(const struct zaclr_type_desc* type,
                                   const struct zaclr_loaded_assembly* assembly,
                                   const char* type_namespace,
                                   size_t type_namespace_length,
                                   const char* type_name)
    {
        size_t index;
        const uint8_t* strings_start;
        const uint8_t* strings_end;

        if (assembly == NULL)
        {
            return false;
        }

        if (type == NULL || type_namespace == NULL || type_name == NULL)
        {
            return false;
        }

        strings_start = assembly->metadata.strings_heap.data;
        strings_end = strings_start != NULL ? (strings_start + assembly->metadata.strings_heap.size) : NULL;
        if (strings_start == NULL || strings_end == NULL)
        {
            return false;
        }

        if (type->type_name.text == NULL
            || (const uint8_t*)type->type_name.text < strings_start
            || ((const uint8_t*)type->type_name.text + type->type_name.length) >= strings_end)
        {
            console_write("[ZACLR][process] invalid type_name view while resolving launch entry\n");
            return false;
        }

        if (type->type_namespace.text != NULL
            && (((const uint8_t*)type->type_namespace.text < strings_start)
                || (((const uint8_t*)type->type_namespace.text + type->type_namespace.length) >= strings_end)))
        {
            console_write("[ZACLR][process] invalid type_namespace view while resolving launch entry\n");
            return false;
        }

        if (!zaclr_name_view_equals(type->type_name, type_name))
        {
            return false;
        }

        if (type->type_namespace.length == 0u)
        {
            return type_namespace_length == 0u;
        }

        if (type->type_namespace.length != type_namespace_length)
        {
            return false;
        }

        for (index = 0u; index < type->type_namespace.length; ++index)
        {
            if (type->type_namespace.text[index] != type_namespace[index])
            {
                return false;
            }
        }

        return true;
    }

    static const struct zaclr_type_desc* zaclr_find_type_by_name(const struct zaclr_loaded_assembly* assembly,
                                                                 const char* full_type_name)
    {
        const char* type_namespace;
        size_t type_namespace_length;
        const char* type_name;
        uint32_t type_index;

        if (assembly == NULL || full_type_name == NULL)
        {
            return NULL;
        }

        zaclr_split_type_name(full_type_name, &type_namespace, &type_namespace_length, &type_name);
        for (type_index = 0u; type_index < assembly->type_map.count; ++type_index)
        {
            const struct zaclr_type_desc* type = &assembly->type_map.types[type_index];
            if (zaclr_type_matches(type, assembly, type_namespace, type_namespace_length, type_name))
            {
                return type;
            }
        }

        zaclr_debug_log_launch_type_resolution(assembly, full_type_name, type_namespace, type_name);
        return NULL;
    }

    static const struct zaclr_method_desc* zaclr_find_method_by_name(const struct zaclr_loaded_assembly* assembly,
                                                                     const struct zaclr_type_desc* type,
                                                                     const char* method_name)
    {
        uint32_t method_index;

        if (assembly == NULL || type == NULL || method_name == NULL)
        {
            return NULL;
        }

        for (method_index = 0u; method_index < type->method_count; ++method_index)
        {
            const struct zaclr_method_desc* method = &assembly->method_map.methods[type->first_method_index + method_index];
            if (zaclr_name_view_equals(method->name, method_name))
            {
                return method;
            }
        }

        return NULL;
    }
}

extern "C" struct zaclr_result zaclr_process_launch_request_validate(const struct zaclr_launch_request* request)
{
    if (request == NULL || request->image_path == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    if ((request->entry_type == NULL) != (request->entry_method == NULL))
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_process_resolve_launch_entry_point(const struct zaclr_loaded_assembly* assembly,
                                                                          const struct zaclr_launch_request* request,
                                                                          struct zaclr_method_locator* out_locator,
                                                                          const struct zaclr_method_desc** out_method)
{
    const struct zaclr_method_desc* method;

    if (assembly == NULL || request == NULL || out_locator == NULL || out_method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    if (request->entry_type != NULL && request->entry_method != NULL)
    {
        const struct zaclr_type_desc* type = zaclr_find_type_by_name(assembly, request->entry_type);
        if (type == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_PROCESS);
        }

        method = zaclr_find_method_by_name(assembly, type, request->entry_method);
    }
    else
    {
        method = zaclr_method_map_find_by_token(&assembly->method_map, assembly->entry_point_token);
    }

    if (method == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_PROCESS);
    }

    out_locator->assembly_id = assembly->id;
    out_locator->method_id = method->id;
    *out_method = method;
    return zaclr_result_ok();
}
