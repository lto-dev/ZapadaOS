#include <kernel/zaclr/loader/zaclr_assembly_registry.h>

#include <kernel/support/kernel_memory.h>

extern "C" {
#include <kernel/console.h>
}

namespace {

static bool name_equals(struct zaclr_name_view view, const char* name)
{
    size_t index;

    if (name == NULL || view.text == NULL) {
        return false;
    }

    for (index = 0u; index < view.length; ++index) {
        if (name[index] == '\0' || view.text[index] != name[index]) {
            return false;
        }
    }

    return name[view.length] == '\0';
}

}

extern "C" struct zaclr_result zaclr_assembly_registry_initialize(struct zaclr_assembly_registry* registry)
{
    if (registry == NULL) {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    registry->entries = NULL;
    registry->count = 0u;
    registry->capacity = 0u;
    registry->flags = 0u;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_assembly_registry_register(struct zaclr_assembly_registry* registry,
                                                                 const struct zaclr_loaded_assembly* assembly)
{
    struct zaclr_loaded_assembly* new_entries;
    uint32_t new_capacity;

    if (registry == NULL || assembly == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (zaclr_assembly_registry_find_by_name(registry, assembly->assembly_name.text) != NULL) {
        console_write("[ZACLR][loader] AssemblyRegistry.AlreadyExists name=");
        console_write(assembly->assembly_name.text != NULL ? assembly->assembly_name.text : "<null>");
        console_write(" id=");
        console_write_dec((uint64_t)assembly->id);
        console_write("\n");
        return zaclr_result_make(ZACLR_STATUS_ALREADY_EXISTS, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (registry->count == registry->capacity) {
        new_capacity = registry->capacity == 0u ? 4u : registry->capacity * 2u;
        new_entries = (struct zaclr_loaded_assembly*)kernel_alloc(sizeof(struct zaclr_loaded_assembly) * new_capacity);
        if (new_entries == NULL) {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_LOADER);
        }

        kernel_memset(new_entries, 0, sizeof(struct zaclr_loaded_assembly) * new_capacity);
        if (registry->entries != NULL) {
            kernel_memcpy(new_entries, registry->entries, sizeof(struct zaclr_loaded_assembly) * registry->count);
            /*
             * Do NOT call kernel_free(registry->entries) here.
             *
             * The zaclr_loaded_assembly structs are stored by value in a flat array.
             * Pointers of type const struct zaclr_loaded_assembly* are kept by many
             * long-lived structures: boot_launch.assembly, frame->assembly in every
             * active or past frame, zaclr_method_table::assembly, managed object
             * headers (owning_assembly), GC roots, and the interop cache entries.
             *
             * Freeing the old array returns it to the kernel heap free list.  A
             * subsequent allocation will zero-fill it (via block_zero_user), turning
             * every stale pointer into a read of all-zeros: id=0, metadata=zeroed,
             * user_string_heap.data=NULL, etc.  This silently breaks ldstr, interop
             * cache lookups, and GCHandle operations the moment those stale pointers
             * are dereferenced.
             *
             * Because the kernel never unloads assemblies and the registry grows at
             * most a handful of times during startup, leaking the old array
             * (typically 4 x ~2.9 KB = ~12 KB per grow) is the correct and safe
             * trade-off.  All existing pointers remain permanently valid.
             */
        }

        registry->entries = new_entries;
        registry->capacity = new_capacity;
    }

    registry->entries[registry->count] = *assembly;
    registry->count += 1u;
    return zaclr_result_ok();
}

extern "C" const struct zaclr_loaded_assembly* zaclr_assembly_registry_find_by_name(const struct zaclr_assembly_registry* registry,
                                                                                      const char* assembly_name)
{
    uint32_t index;

    if (registry == NULL || assembly_name == NULL) {
        return NULL;
    }

    /* console_write("[ZACLR][registry] find_by_name target="); */
    /* console_write(assembly_name); */
    /* console_write(" count="); */
    /* console_write_dec((uint64_t)registry->count); */
    /* console_write(" capacity="); */
    /* console_write_dec((uint64_t)registry->capacity); */
    /* console_write(" entries="); */
    /* console_write_hex64((uint64_t)(uintptr_t)registry->entries); */
    /* console_write("\n"); */

    for (index = 0u; index < registry->count; ++index) {
        /* console_write("[ZACLR][registry] check index="); */
        /* console_write_dec((uint64_t)index); */
        /* console_write(" id="); */
        /* console_write_dec((uint64_t)registry->entries[index].id); */
        /* console_write(" name_ptr="); */
        /* console_write_hex64((uint64_t)(uintptr_t)registry->entries[index].assembly_name.text); */
        /* console_write(" len="); */
        /* console_write_dec((uint64_t)registry->entries[index].assembly_name.length); */
        /* console_write("\n"); */
        if (name_equals(registry->entries[index].assembly_name, assembly_name)) {
            /* console_write("[ZACLR][registry] match found\n"); */
            return &registry->entries[index];
        }
    }

    /* console_write("[ZACLR][registry] no match\n"); */

    return NULL;
}

extern "C" const struct zaclr_loaded_assembly* zaclr_assembly_registry_find_by_id(const struct zaclr_assembly_registry* registry,
                                                                                     zaclr_assembly_id assembly_id)
{
    uint32_t index;

    if (registry == NULL || assembly_id == 0u) {
        return NULL;
    }

    for (index = 0u; index < registry->count; ++index) {
        if (registry->entries[index].id == assembly_id) {
            return &registry->entries[index];
        }
    }

    return NULL;
}

extern "C" void zaclr_assembly_registry_reset(struct zaclr_assembly_registry* registry)
{
    uint32_t index;

    if (registry == NULL) {
        return;
    }

    for (index = 0u; index < registry->count; ++index) {
        zaclr_loader_release_loaded_assembly(&registry->entries[index]);
    }

    if (registry->entries != NULL) {
        kernel_free(registry->entries);
    }

    registry->entries = NULL;
    registry->count = 0u;
    registry->capacity = 0u;
    registry->flags = 0u;
}
