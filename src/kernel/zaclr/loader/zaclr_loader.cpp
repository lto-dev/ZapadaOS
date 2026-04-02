#include <kernel/zaclr/loader/zaclr_loader.h>

#include <kernel/support/kernel_memory.h>

extern "C" struct zaclr_result zaclr_loader_initialize(struct zaclr_loader* loader)
{
    if (loader == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    loader->next_assembly_id = 1u;
    loader->flags = 0u;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_loader_load_image(struct zaclr_loader* loader,
                                                        const struct zaclr_slice* image,
                                                        struct zaclr_loaded_assembly* loaded_assembly)
{
    struct zaclr_result result;
    uint32_t static_field_count;

    if (loader == NULL || image == NULL || loaded_assembly == NULL) {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    *loaded_assembly = {};

    result = zaclr_pe_image_parse(image, &loaded_assembly->image);
    if (result.status != ZACLR_STATUS_OK) {
        return result;
    }

    result = zaclr_metadata_reader_initialize(&loaded_assembly->metadata, &loaded_assembly->image.metadata_root);
    if (result.status != ZACLR_STATUS_OK) {
        zaclr_loader_release_loaded_assembly(loaded_assembly);
        return result;
    }

    result = zaclr_method_map_initialize(&loaded_assembly->method_map, &loaded_assembly->metadata);
    if (result.status != ZACLR_STATUS_OK) {
        zaclr_loader_release_loaded_assembly(loaded_assembly);
        return result;
    }

    result = zaclr_type_map_initialize(&loaded_assembly->type_map, &loaded_assembly->metadata, &loaded_assembly->method_map);
    if (result.status != ZACLR_STATUS_OK) {
        zaclr_loader_release_loaded_assembly(loaded_assembly);
        return result;
    }

    static_field_count = zaclr_metadata_reader_get_row_count(&loaded_assembly->metadata, ZACLR_TOKEN_TABLE_FIELD);
    loaded_assembly->static_field_count = static_field_count;
    if (static_field_count != 0u) {
        loaded_assembly->static_fields = (struct zaclr_stack_value*)kernel_alloc(sizeof(struct zaclr_stack_value) * static_field_count);
        if (loaded_assembly->static_fields == NULL) {
            zaclr_loader_release_loaded_assembly(loaded_assembly);
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_LOADER);
        }

        kernel_memset(loaded_assembly->static_fields, 0, sizeof(struct zaclr_stack_value) * static_field_count);
    }

    loaded_assembly->id = loader->next_assembly_id++;
    loaded_assembly->assembly_name = loaded_assembly->image.assembly_name;
    loaded_assembly->entry_point_token = zaclr_token_make(loaded_assembly->image.entry_point_token);
    return zaclr_result_ok();
}

extern "C" void zaclr_loader_release_loaded_assembly(struct zaclr_loaded_assembly* loaded_assembly)
{
    if (loaded_assembly == NULL) {
        return;
    }

    if (loaded_assembly->static_fields != NULL) {
        kernel_free(loaded_assembly->static_fields);
    }

    zaclr_type_map_reset(&loaded_assembly->type_map);
    zaclr_method_map_reset(&loaded_assembly->method_map);
    kernel_memset(loaded_assembly, 0, sizeof(*loaded_assembly));
}
