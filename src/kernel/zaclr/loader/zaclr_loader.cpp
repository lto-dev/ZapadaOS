#include <kernel/zaclr/loader/zaclr_loader.h>
#include <kernel/zaclr/typesystem/zaclr_method_table.h>

#include <kernel/support/kernel_memory.h>

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
}

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
    uint32_t runtime_type_cache_count;

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

    if (loaded_assembly->type_map.count != 0u) {
        loaded_assembly->type_initializer_state = (uint8_t*)kernel_alloc(loaded_assembly->type_map.count);
        if (loaded_assembly->type_initializer_state == NULL) {
            zaclr_loader_release_loaded_assembly(loaded_assembly);
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_LOADER);
        }

        kernel_memset(loaded_assembly->type_initializer_state, 0, loaded_assembly->type_map.count);
    }

    runtime_type_cache_count = zaclr_metadata_reader_get_row_count(&loaded_assembly->metadata, ZACLR_TOKEN_TABLE_TYPEDEF);
    loaded_assembly->runtime_type_cache_count = runtime_type_cache_count;
    if (runtime_type_cache_count != 0u)
    {
        loaded_assembly->runtime_type_cache = (zaclr_object_handle*)kernel_alloc(sizeof(zaclr_object_handle) * runtime_type_cache_count);
        if (loaded_assembly->runtime_type_cache == NULL)
        {
            zaclr_loader_release_loaded_assembly(loaded_assembly);
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_LOADER);
        }

        kernel_memset(loaded_assembly->runtime_type_cache, 0, sizeof(zaclr_object_handle) * runtime_type_cache_count);
    }

    {
        uint32_t typedef_count = zaclr_metadata_reader_get_row_count(&loaded_assembly->metadata, ZACLR_TOKEN_TABLE_TYPEDEF);
        loaded_assembly->method_table_cache_count = typedef_count;
        if (typedef_count != 0u)
        {
            loaded_assembly->method_table_cache = (struct zaclr_method_table**)kernel_alloc(sizeof(struct zaclr_method_table*) * typedef_count);
            if (loaded_assembly->method_table_cache == NULL)
            {
                zaclr_loader_release_loaded_assembly(loaded_assembly);
                return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_LOADER);
            }

            kernel_memset(loaded_assembly->method_table_cache, 0, sizeof(struct zaclr_method_table*) * typedef_count);
        }
    }

    loaded_assembly->id = loader->next_assembly_id++;
    loaded_assembly->assembly_name = loaded_assembly->image.assembly_name;
    loaded_assembly->entry_point_token = zaclr_token_make(loaded_assembly->image.entry_point_token);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_loader_apply_assembly_name_fallback(struct zaclr_loaded_assembly* loaded_assembly,
                                                                           const char* image_path)
{
    size_t source_length;
    size_t name_length = 0u;
    char* storage;

    if (loaded_assembly == NULL || image_path == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (loaded_assembly->assembly_name.text != NULL && loaded_assembly->assembly_name.length != 0u)
    {
        return zaclr_result_ok();
    }

    source_length = text_length(image_path);
    while (name_length < source_length && image_path[name_length] != '.' && image_path[name_length] != '\0')
    {
        ++name_length;
    }

    storage = (char*)kernel_alloc(name_length + 1u);
    if (storage == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_LOADER);
    }

    if (name_length != 0u)
    {
        kernel_memcpy(storage, image_path, name_length);
    }

    storage[name_length] = '\0';
    if (loaded_assembly->assembly_name_storage != NULL)
    {
        kernel_free(loaded_assembly->assembly_name_storage);
    }

    loaded_assembly->assembly_name_storage = storage;
    loaded_assembly->assembly_name.text = storage;
    loaded_assembly->assembly_name.length = (uint32_t)name_length;
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

    if (loaded_assembly->type_initializer_state != NULL) {
        kernel_free(loaded_assembly->type_initializer_state);
    }

    if (loaded_assembly->runtime_type_cache != NULL) {
        kernel_free(loaded_assembly->runtime_type_cache);
    }

    if (loaded_assembly->method_table_cache != NULL) {
        uint32_t mt_index;
        for (mt_index = 0u; mt_index < loaded_assembly->method_table_cache_count; ++mt_index) {
            if (loaded_assembly->method_table_cache[mt_index] != NULL) {
                struct zaclr_method_table* mt = loaded_assembly->method_table_cache[mt_index];
                if (mt->vtable != NULL) {
                    kernel_free((void*)mt->vtable);
                }
                if (mt->instance_fields != NULL) {
                    kernel_free(mt->instance_fields);
                }
                if (mt->static_fields != NULL) {
                    kernel_free(mt->static_fields);
                }
                kernel_free(mt);
            }
        }
        kernel_free(loaded_assembly->method_table_cache);
    }

    if (loaded_assembly->assembly_name_storage != NULL) {
        kernel_free(loaded_assembly->assembly_name_storage);
    }

    zaclr_type_map_reset(&loaded_assembly->type_map);
    zaclr_method_map_reset(&loaded_assembly->method_map);
    kernel_memset(loaded_assembly, 0, sizeof(*loaded_assembly));
}
