#include <kernel/zaclr/loader/zaclr_assembly_source_initramfs.h>

extern "C" {
#include <kernel/initramfs/ramdisk.h>
}

namespace
{
    static bool names_match(const char* left, const char* right)
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

    static struct zaclr_result initramfs_locate(void* context,
                                                const struct zaclr_assembly_identity* identity,
                                                struct zaclr_assembly_image* out_image)
    {
        ramdisk_file_t file = {};
        char image_path[128];
        uint32_t name_length;
        const char suffix[] = ".dll";

        (void)context;

        if (identity == NULL || out_image == NULL || identity->name == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
        }

        *out_image = {};
        name_length = identity->name_length;
        if (name_length == 0u)
        {
            while (identity->name[name_length] != '\0')
            {
                ++name_length;
            }
        }

        if ((size_t)name_length + sizeof(suffix) > sizeof(image_path))
        {
            return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_LOADER);
        }

        for (uint32_t index = 0u; index < name_length; ++index)
        {
            image_path[index] = identity->name[index];
        }

        for (uint32_t suffix_index = 0u; suffix[suffix_index] != '\0'; ++suffix_index)
        {
            image_path[name_length + suffix_index] = suffix[suffix_index];
        }

        image_path[name_length + 4u] = '\0';

        if (ramdisk_lookup(image_path, &file) == RAMDISK_OK)
        {
            out_image->data = file.data;
            out_image->size = file.size;
            out_image->source_label = file.filename;
            out_image->caller_owns = false;
            return zaclr_result_ok();
        }

        for (uint32_t file_index = 0u; file_index < ramdisk_file_count(); ++file_index)
        {
            ramdisk_file_t candidate = {};

            if (ramdisk_get_file(file_index, &candidate) != RAMDISK_OK || candidate.filename == NULL)
            {
                continue;
            }

            if (ramdisk_lookup(candidate.filename, &file) != RAMDISK_OK)
            {
                continue;
            }

            if (!names_match(candidate.filename, image_path))
            {
                continue;
            }

            out_image->data = file.data;
            out_image->size = file.size;
            out_image->source_label = file.filename;
            out_image->caller_owns = false;
            return zaclr_result_ok();
        }

        return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_LOADER);
    }

    static void initramfs_release(void* context, struct zaclr_assembly_image* image)
    {
        (void)context;
        (void)image;
    }

    static const struct zaclr_assembly_source g_initramfs_source = {
        "initramfs",
        initramfs_locate,
        initramfs_release,
        NULL
    };
}

extern "C" const struct zaclr_assembly_source* zaclr_assembly_source_initramfs(void)
{
    return &g_initramfs_source;
}
