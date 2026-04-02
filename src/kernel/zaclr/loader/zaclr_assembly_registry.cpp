#include <kernel/zaclr/loader/zaclr_assembly_registry.h>

#include <kernel/support/kernel_memory.h>

namespace {

static bool name_equals(struct zaclr_name_view view, const char* name)
{
    size_t index;

    if (name == NULL) {
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
            kernel_free(registry->entries);
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

    for (index = 0u; index < registry->count; ++index) {
        if (name_equals(registry->entries[index].assembly_name, assembly_name)) {
            return &registry->entries[index];
        }
    }

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
