#include <kernel/zaclr/loader/zaclr_binder.h>

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
}

extern "C" struct zaclr_result zaclr_binder_load_assembly_by_path(struct zaclr_runtime* runtime,
                                                                    const char* image_path,
                                                                    const struct zaclr_loaded_assembly** out_assembly)
{
    ramdisk_file_t file;
    struct zaclr_slice image;
    struct zaclr_loaded_assembly loaded_assembly;
    const struct zaclr_loaded_assembly* assembly;
    struct zaclr_result result;

    if (runtime == NULL || image_path == NULL || out_assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (ramdisk_lookup(image_path, &file) != RAMDISK_OK)
    {
        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_LOADER);
    }

    image.data = file.data;
    image.size = file.size;
    result = zaclr_loader_load_image(&runtime->loader, &image, &loaded_assembly);
    if (result.status != ZACLR_STATUS_OK)
    {
        return result;
    }

    assembly = zaclr_assembly_registry_find_by_name(&runtime->assemblies, loaded_assembly.assembly_name.text);
    if (assembly != NULL)
    {
        zaclr_loader_release_loaded_assembly(&loaded_assembly);
        *out_assembly = assembly;
        return zaclr_result_ok();
    }

    result = zaclr_assembly_registry_register(&runtime->assemblies, &loaded_assembly);
    if (result.status != ZACLR_STATUS_OK)
    {
        zaclr_loader_release_loaded_assembly(&loaded_assembly);
        return result;
    }

    assembly = runtime->assemblies.count != 0u ? &runtime->assemblies.entries[runtime->assemblies.count - 1u] : NULL;
    if (assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
    }

    *out_assembly = assembly;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_binder_load_assembly_by_name(struct zaclr_runtime* runtime,
                                                                    const char* assembly_name,
                                                                    const struct zaclr_loaded_assembly** out_assembly)
{
    char image_path[128];
    size_t length = 0u;
    const struct zaclr_loaded_assembly* assembly;
    struct zaclr_result result;
    const char suffix[] = ".dll";

    if (runtime == NULL || assembly_name == NULL || out_assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    assembly = zaclr_assembly_registry_find_by_name(&runtime->assemblies, assembly_name);
    if (assembly != NULL)
    {
        *out_assembly = assembly;
        return zaclr_result_ok();
    }

    for (; assembly_name[length] != '\0'; ++length)
    {
        if (length + 1u >= sizeof(image_path))
        {
            return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_LOADER);
        }

        image_path[length] = assembly_name[length];
    }

    for (size_t suffix_index = 0u; suffix[suffix_index] != '\0'; ++suffix_index, ++length)
    {
        if (length + 1u >= sizeof(image_path))
        {
            return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_LOADER);
        }

        image_path[length] = suffix[suffix_index];
    }

    image_path[length] = '\0';
    result = zaclr_binder_load_assembly_by_path(runtime, image_path, out_assembly);
    if (result.status == ZACLR_STATUS_OK)
    {
        return result;
    }

    for (uint32_t file_index = 0u; file_index < ramdisk_file_count(); ++file_index)
    {
        ramdisk_file_t candidate = {};
        const struct zaclr_loaded_assembly* loaded_candidate;

        if (ramdisk_get_file(file_index, &candidate) != RAMDISK_OK || candidate.filename == NULL)
        {
            continue;
        }

        if (zaclr_binder_load_assembly_by_path(runtime, candidate.filename, &loaded_candidate).status != ZACLR_STATUS_OK)
        {
            continue;
        }

        if (text_equals(loaded_candidate->assembly_name.text, assembly_name))
        {
            *out_assembly = loaded_candidate;
            return zaclr_result_ok();
        }
    }

    return result;
}
