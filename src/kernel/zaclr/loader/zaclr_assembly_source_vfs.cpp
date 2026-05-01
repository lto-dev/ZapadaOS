#include <kernel/zaclr/loader/zaclr_assembly_source_vfs.h>

extern "C" {
#include <kernel/console.h>
#include <kernel/support/kernel_memory.h>
}

namespace
{
    static const uint32_t ZACLR_VFS_SOURCE_MAX_CACHE_ENTRIES = 32u;
    static const size_t ZACLR_VFS_SOURCE_MAX_IMAGE_SIZE = 32u * 1024u * 1024u;

    struct vfs_cached_image
    {
        char path[128];
        uint8_t* data;
        size_t size;
        bool used;
    };

    struct vfs_pending_publish
    {
        char path[128];
        uint8_t* data;
        size_t size;
        size_t written;
        bool active;
    };

    struct vfs_source_context
    {
        char root_path[96];
        bool configured;
        vfs_cached_image cache[ZACLR_VFS_SOURCE_MAX_CACHE_ENTRIES];
        vfs_pending_publish pending;
    };

    static vfs_source_context g_vfs_source_context = {};

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

    static bool text_equals(const char* left, const char* right)
    {
        size_t index = 0u;

        if (left == NULL || right == NULL)
        {
            return left == right;
        }

        while (left[index] != '\0' && right[index] != '\0')
        {
            if (left[index] != right[index])
            {
                return false;
            }

            ++index;
        }

        return left[index] == '\0' && right[index] == '\0';
    }

    static bool append_text(char* buffer, size_t buffer_size, size_t* cursor, const char* text)
    {
        size_t index;

        if (buffer == NULL || cursor == NULL || text == NULL || *cursor >= buffer_size)
        {
            return false;
        }

        for (index = 0u; text[index] != '\0'; ++index)
        {
            if ((*cursor + 1u) >= buffer_size)
            {
                return false;
            }

            buffer[*cursor] = text[index];
            ++(*cursor);
        }

        buffer[*cursor] = '\0';
        return true;
    }

    static bool build_candidate_path(const char* prefix,
                                     const struct zaclr_assembly_identity* identity,
                                     char* out_path,
                                     size_t out_path_size)
    {
        size_t cursor = 0u;

        if (identity == NULL || identity->name == NULL || out_path == NULL || out_path_size == 0u)
        {
            return false;
        }

        out_path[0] = '\0';
        if (!append_text(out_path, out_path_size, &cursor, prefix))
        {
            return false;
        }

        if (cursor == 0u || out_path[cursor - 1u] != '/')
        {
            if ((cursor + 1u) >= out_path_size)
            {
                return false;
            }

            out_path[cursor] = '/';
            ++cursor;
            out_path[cursor] = '\0';
        }

        if (!append_text(out_path, out_path_size, &cursor, identity->name))
        {
            return false;
        }

        return append_text(out_path, out_path_size, &cursor, ".dll");
    }

    static vfs_cached_image* find_cached_path(vfs_source_context* context, const char* path)
    {
        uint32_t index;

        if (context == NULL || path == NULL)
        {
            return NULL;
        }

        for (index = 0u; index < ZACLR_VFS_SOURCE_MAX_CACHE_ENTRIES; ++index)
        {
            if (context->cache[index].used && text_equals(context->cache[index].path, path))
            {
                return &context->cache[index];
            }
        }

        return NULL;
    }

    static vfs_cached_image* allocate_cache_slot(vfs_source_context* context, const char* path)
    {
        uint32_t index;
        vfs_cached_image* existing;

        if (context == NULL || path == NULL)
        {
            return NULL;
        }

        existing = find_cached_path(context, path);
        if (existing != NULL)
        {
            return existing;
        }

        for (index = 0u; index < ZACLR_VFS_SOURCE_MAX_CACHE_ENTRIES; ++index)
        {
            if (!context->cache[index].used)
            {
                return &context->cache[index];
            }
        }

        return NULL;
    }

    static void clear_pending_publish(vfs_pending_publish* pending)
    {
        if (pending == NULL)
        {
            return;
        }

        if (pending->data != NULL)
        {
            kernel_free(pending->data);
        }

        kernel_memset(pending->path, 0, sizeof(pending->path));
        pending->data = NULL;
        pending->size = 0u;
        pending->written = 0u;
        pending->active = false;
    }

    static const vfs_cached_image* find_cached_assembly(vfs_source_context* context,
                                                        const struct zaclr_assembly_identity* identity)
    {
        static const char* search_prefixes[] = {
            "/boot/zapada",
            "/lib/dotnet",
            "/lib/zapada",
            "/sbin",
            "/bin",
            "/"
        };
        char candidate[128];
        size_t index;

        if (context == NULL || identity == NULL || identity->name == NULL)
        {
            return NULL;
        }

        for (index = 0u; index < (sizeof(search_prefixes) / sizeof(search_prefixes[0])); ++index)
        {
            if (!build_candidate_path(search_prefixes[index], identity, candidate, sizeof(candidate)))
            {
                continue;
            }

            const vfs_cached_image* cached = find_cached_path(context, candidate);
            if (cached != NULL)
            {
                return cached;
            }
        }

        return NULL;
    }

    static struct zaclr_result vfs_locate(void* context,
                                          const struct zaclr_assembly_identity* identity,
                                          struct zaclr_assembly_image* out_image)
    {
        vfs_source_context* vfs_context = (vfs_source_context*)context;

        if (out_image != NULL)
        {
            *out_image = {};
        }

        if (vfs_context == NULL || identity == NULL || identity->name == NULL || out_image == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
        }

        if (!vfs_context->configured)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
        }

        const vfs_cached_image* cached = find_cached_assembly(vfs_context, identity);
        if (cached == NULL || cached->data == NULL || cached->size == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_LOADER);
        }

        out_image->data = cached->data;
        out_image->size = cached->size;
        out_image->source_label = cached->path;
        out_image->caller_owns = false;
        return zaclr_result_ok();
    }

    static void vfs_release(void* context, struct zaclr_assembly_image* image)
    {
        (void)context;
        (void)image;
    }

    static struct zaclr_assembly_source g_vfs_source = {
        "vfs",
        vfs_locate,
        vfs_release,
        &g_vfs_source_context
    };
}

extern "C" struct zaclr_assembly_source* zaclr_assembly_source_vfs(void)
{
    return &g_vfs_source;
}

extern "C" struct zaclr_result zaclr_assembly_source_vfs_configure(const char* root_path)
{
    size_t length;

    if (root_path == NULL || root_path[0] != '/')
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    length = text_length(root_path);
    if (length == 0u || length >= sizeof(g_vfs_source_context.root_path))
    {
        return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_LOADER);
    }

    kernel_memset(g_vfs_source_context.root_path, 0, sizeof(g_vfs_source_context.root_path));
    kernel_memcpy(g_vfs_source_context.root_path, root_path, length);
    g_vfs_source_context.root_path[length] = '\0';
    g_vfs_source_context.configured = true;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_assembly_source_vfs_publish(const char* path,
                                                                    const uint8_t* data,
                                                                    size_t size)
{
    size_t path_length;
    uint8_t* copy;
    vfs_cached_image* slot;

    if (path == NULL || path[0] != '/' || data == NULL || size == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (size > ZACLR_VFS_SOURCE_MAX_IMAGE_SIZE)
    {
        return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_LOADER);
    }

    path_length = text_length(path);
    if (path_length == 0u || path_length >= sizeof(g_vfs_source_context.cache[0].path))
    {
        return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_LOADER);
    }

    slot = allocate_cache_slot(&g_vfs_source_context, path);
    if (slot == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_LOADER);
    }

    copy = (uint8_t*)kernel_alloc(size);
    if (copy == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_LOADER);
    }

    kernel_memcpy(copy, data, size);

    if (slot->data != NULL)
    {
        kernel_free(slot->data);
    }

    kernel_memset(slot->path, 0, sizeof(slot->path));
    kernel_memcpy(slot->path, path, path_length);
    slot->path[path_length] = '\0';
    slot->data = copy;
    slot->size = size;
    slot->used = true;

    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_assembly_source_vfs_publish_begin(const char* path,
                                                                        size_t size)
{
    size_t path_length;
    uint8_t* data;
    vfs_cached_image* slot;

    if (path == NULL || path[0] != '/' || size == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (size > ZACLR_VFS_SOURCE_MAX_IMAGE_SIZE)
    {
        return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (g_vfs_source_context.pending.active)
    {
        return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
    }

    path_length = text_length(path);
    if (path_length == 0u || path_length >= sizeof(g_vfs_source_context.pending.path))
    {
        return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_LOADER);
    }

    slot = allocate_cache_slot(&g_vfs_source_context, path);
    if (slot == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_LOADER);
    }

    data = (uint8_t*)kernel_alloc(size);
    if (data == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_LOADER);
    }

    kernel_memset(g_vfs_source_context.pending.path, 0, sizeof(g_vfs_source_context.pending.path));
    kernel_memcpy(g_vfs_source_context.pending.path, path, path_length);
    g_vfs_source_context.pending.path[path_length] = '\0';
    g_vfs_source_context.pending.data = data;
    g_vfs_source_context.pending.size = size;
    g_vfs_source_context.pending.written = 0u;
    g_vfs_source_context.pending.active = true;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_assembly_source_vfs_publish_append(const char* path,
                                                                         const uint8_t* data,
                                                                         size_t size)
{
    vfs_pending_publish* pending = &g_vfs_source_context.pending;

    if (path == NULL || data == NULL || size == 0u)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (!pending->active || pending->data == NULL || !text_equals(pending->path, path))
    {
        return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (pending->written > pending->size || size > (pending->size - pending->written))
    {
        clear_pending_publish(pending);
        return zaclr_result_make(ZACLR_STATUS_BUFFER_TOO_SMALL, ZACLR_STATUS_CATEGORY_LOADER);
    }

    kernel_memcpy(pending->data + pending->written, data, size);
    pending->written += size;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_assembly_source_vfs_publish_end(const char* path)
{
    vfs_pending_publish* pending = &g_vfs_source_context.pending;
    vfs_cached_image* slot;

    if (path == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (!pending->active || pending->data == NULL || !text_equals(pending->path, path))
    {
        return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (pending->written != pending->size)
    {
        return zaclr_result_make(ZACLR_STATUS_BAD_STATE, ZACLR_STATUS_CATEGORY_LOADER);
    }

    slot = allocate_cache_slot(&g_vfs_source_context, path);
    if (slot == NULL)
    {
        clear_pending_publish(pending);
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (slot->data != NULL)
    {
        kernel_free(slot->data);
    }

    kernel_memset(slot->path, 0, sizeof(slot->path));
    kernel_memcpy(slot->path, pending->path, text_length(pending->path));
    slot->data = pending->data;
    slot->size = pending->size;
    slot->used = true;

    pending->data = NULL;
    pending->size = 0u;
    pending->written = 0u;
    pending->active = false;
    kernel_memset(pending->path, 0, sizeof(pending->path));
    return zaclr_result_ok();
}

extern "C" const char* zaclr_assembly_source_vfs_root(void)
{
    return g_vfs_source_context.configured ? g_vfs_source_context.root_path : NULL;
}
