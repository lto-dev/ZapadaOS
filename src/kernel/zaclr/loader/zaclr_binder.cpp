#include <kernel/zaclr/loader/zaclr_binder.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static size_t text_length(const char* text)
    {
        size_t length = 0u;

        if (text == NULL)
        {
            return 0u;
        }

        while (text[length] != '\0')
        {
            ++length;
        }

        return length;
    }

    static struct zaclr_result build_identity_from_name(const char* assembly_name,
                                                        struct zaclr_assembly_identity* identity)
    {
        if (assembly_name == NULL || identity == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
        }

        *identity = {};
        identity->name = assembly_name;
        identity->name_length = (uint32_t)text_length(assembly_name);
        return zaclr_result_ok();
    }

    static struct zaclr_result build_identity_from_path(const char* image_path,
                                                        char* name_buffer,
                                                        size_t name_buffer_size,
                                                        struct zaclr_assembly_identity* identity)
    {
        size_t name_length = 0u;

        if (image_path == NULL || name_buffer == NULL || name_buffer_size == 0u || identity == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
        }

        while (image_path[name_length] != '\0' && image_path[name_length] != '.')
        {
            if (name_length + 1u >= name_buffer_size)
            {
                return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_LOADER);
            }

            name_buffer[name_length] = image_path[name_length];
            ++name_length;
        }

        name_buffer[name_length] = '\0';
        *identity = {};
        identity->name = name_buffer;
        identity->name_length = (uint32_t)name_length;
        return zaclr_result_ok();
    }

    static const struct zaclr_loaded_assembly* registry_last_entry(const struct zaclr_assembly_registry* registry)
    {
        if (registry == NULL || registry->count == 0u || registry->entries == NULL)
        {
            return NULL;
        }

        return &registry->entries[registry->count - 1u];
    }

    static struct zaclr_result register_runtime_compat_assembly(struct zaclr_runtime* runtime,
                                                                const struct zaclr_loaded_assembly* assembly,
                                                                const struct zaclr_loaded_assembly** out_assembly)
    {
        const struct zaclr_loaded_assembly* compat_assembly;
        struct zaclr_result result;

        if (runtime == NULL || assembly == NULL || out_assembly == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
        }

        compat_assembly = zaclr_assembly_registry_find_by_name(&runtime->assemblies, assembly->assembly_name.text);
        if (compat_assembly != NULL)
        {
            *out_assembly = compat_assembly;
            return zaclr_result_ok();
        }

        result = zaclr_assembly_registry_register(&runtime->assemblies, assembly);
        if (result.status == ZACLR_STATUS_ALREADY_EXISTS)
        {
            compat_assembly = zaclr_assembly_registry_find_by_name(&runtime->assemblies, assembly->assembly_name.text);
            if (compat_assembly == NULL)
            {
                return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
            }

            *out_assembly = compat_assembly;
            return zaclr_result_ok();
        }

        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        compat_assembly = registry_last_entry(&runtime->assemblies);
        if (compat_assembly == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
        }

        *out_assembly = compat_assembly;
        return zaclr_result_ok();
    }
}

extern "C" struct zaclr_result zaclr_binder_bind(struct zaclr_loader* loader,
                                                   struct zaclr_app_domain* domain,
                                                   const struct zaclr_assembly_identity* identity,
                                                   const struct zaclr_loaded_assembly** out_assembly)
{
    const struct zaclr_loaded_assembly* assembly;
    struct zaclr_assembly_image assembly_image;
    struct zaclr_loaded_assembly loaded_assembly;
    struct zaclr_result result;
    const char* source_name;

    if (loader == NULL || domain == NULL || identity == NULL || identity->name == NULL || out_assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    console_write("[ZACLR][binder] bind begin name=");
    console_write(identity->name);
    console_write(" domain=");
    console_write_dec((uint64_t)domain->id);
    console_write("\n");

    assembly = zaclr_assembly_registry_find_by_name(&domain->registry, identity->name);
    console_write("[ZACLR][binder] domain registry result=");
    console_write(assembly != NULL ? "hit" : "miss");
    console_write("\n");
    if (assembly != NULL)
    {
        *out_assembly = assembly;
        return zaclr_result_ok();
    }

    if (domain->source == NULL || domain->source->locate == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_LOADER);
    }

    assembly_image = {};
    source_name = domain->source->name != NULL ? domain->source->name : "<unnamed>";
    console_write("[ZACLR][binder] locate source=");
    console_write(source_name);
    console_write("\n");
    result = domain->source->locate(domain->source->context, identity, &assembly_image);
    console_write("[ZACLR][binder] locate status=");
    console_write_dec((uint64_t)result.status);
    console_write("\n");
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    {
        struct zaclr_slice slice = {};
        slice.data = assembly_image.data;
        slice.size = assembly_image.size;
        result = zaclr_loader_load_image(loader, &slice, &loaded_assembly);
    }
    console_write("[ZACLR][binder] parse status=");
    console_write_dec((uint64_t)result.status);
    console_write("\n");
    if (result.status != ZACLR_STATUS_OK)
    {
        if (assembly_image.caller_owns && domain->source->release != NULL)
        {
            domain->source->release(domain->source->context, &assembly_image);
        }

        return result;
    }

    result = zaclr_loader_apply_assembly_name_fallback(&loaded_assembly,
                                                       assembly_image.source_label != NULL ? assembly_image.source_label : identity->name);
    console_write("[ZACLR][binder] name fallback status=");
    console_write_dec((uint64_t)result.status);
    console_write("\n");
    if (result.status != ZACLR_STATUS_OK)
    {
        zaclr_loader_release_loaded_assembly(&loaded_assembly);
        if (assembly_image.caller_owns && domain->source->release != NULL)
        {
            domain->source->release(domain->source->context, &assembly_image);
        }

        return result;
    }

    result = zaclr_assembly_registry_register(&domain->registry, &loaded_assembly);
    console_write("[ZACLR][binder] domain register status=");
    console_write_dec((uint64_t)result.status);
    console_write("\n");
    if (result.status == ZACLR_STATUS_ALREADY_EXISTS)
    {
        assembly = zaclr_assembly_registry_find_by_name(&domain->registry, loaded_assembly.assembly_name.text);
        zaclr_loader_release_loaded_assembly(&loaded_assembly);
        if (assembly_image.caller_owns && domain->source->release != NULL)
        {
            domain->source->release(domain->source->context, &assembly_image);
        }

        if (assembly == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
        }

        *out_assembly = assembly;
        return zaclr_result_ok();
    }

    if (result.status != ZACLR_STATUS_OK)
    {
        zaclr_loader_release_loaded_assembly(&loaded_assembly);
        if (assembly_image.caller_owns && domain->source->release != NULL)
        {
            domain->source->release(domain->source->context, &assembly_image);
        }

        return result;
    }

    if (assembly_image.caller_owns && domain->source->release != NULL)
    {
        domain->source->release(domain->source->context, &assembly_image);
    }

    assembly = registry_last_entry(&domain->registry);
    if (assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
    }

    *out_assembly = assembly;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_binder_load_assembly_by_path(struct zaclr_runtime* runtime,
                                                                     const char* image_path,
                                                                     const struct zaclr_loaded_assembly** out_assembly)
{
    char assembly_name[128];
    struct zaclr_assembly_identity identity;
    const struct zaclr_loaded_assembly* domain_assembly;
    struct zaclr_result result;

    if (runtime == NULL || image_path == NULL || out_assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    result = build_identity_from_path(image_path, assembly_name, sizeof(assembly_name), &identity);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    console_write("[ZACLR][binder] load_by_path candidate=");
    console_write(image_path);
    console_write(" identity=");
    console_write(identity.name);
    console_write("\n");

    result = zaclr_binder_bind(&runtime->loader, &runtime->boot_launch.domain, &identity, &domain_assembly);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    return register_runtime_compat_assembly(runtime, domain_assembly, out_assembly);
}

extern "C" struct zaclr_result zaclr_binder_load_assembly_by_name(struct zaclr_runtime* runtime,
                                                                     const char* assembly_name,
                                                                     const struct zaclr_loaded_assembly** out_assembly)
{
    struct zaclr_assembly_identity identity;
    const struct zaclr_loaded_assembly* domain_assembly;
    struct zaclr_result result;

    if (runtime == NULL || out_assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (assembly_name == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_LOADER);
    }

    console_write("[ZACLR][binder] begin assembly=");
    console_write(assembly_name);
    console_write("\n");

    result = build_identity_from_name(assembly_name, &identity);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = zaclr_binder_bind(&runtime->loader, &runtime->boot_launch.domain, &identity, &domain_assembly);
    console_write("[ZACLR][binder] bind status=");
    console_write_dec((uint64_t)result.status);
    console_write("\n");
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    result = register_runtime_compat_assembly(runtime, domain_assembly, out_assembly);
    console_write("[ZACLR][binder] compat register status=");
    console_write_dec((uint64_t)result.status);
    console_write("\n");
    return result;
}
