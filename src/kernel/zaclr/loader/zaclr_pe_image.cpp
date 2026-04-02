#include <kernel/zaclr/loader/zaclr_pe_image.h>

#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>

namespace {

static uint16_t read_u16(const uint8_t* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t* p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static struct zaclr_result bad_image(void)
{
    return zaclr_result_make(ZACLR_STATUS_BAD_IMAGE, ZACLR_STATUS_CATEGORY_LOADER);
}

static struct zaclr_result invalid_argument(void)
{
    return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_LOADER);
}

}

extern "C" struct zaclr_result zaclr_pe_image_parse(const struct zaclr_slice* image_bytes,
                                                     struct zaclr_pe_image* image)
{
    const uint8_t* data;
    uint32_t pe_offset;
    uint32_t coff_offset;
    uint32_t optional_offset;
    uint32_t optional_size;
    uint16_t section_count;
    uint16_t optional_magic;
    uint32_t data_directory_offset;
    uint32_t cli_offset;
    uint32_t metadata_offset;
    uint32_t cli_size;
    uint32_t metadata_size;
    uint32_t index;
    struct zaclr_result result;
    struct zaclr_metadata_reader metadata_reader;

    if (image_bytes == NULL || image == NULL || image_bytes->data == NULL || image_bytes->size < 0x40u) {
        return invalid_argument();
    }

    *image = {};
    image->image = *image_bytes;
    data = image_bytes->data;

    if (data[0] != 'M' || data[1] != 'Z') {
        return bad_image();
    }

    pe_offset = read_u32(data + 0x3Cu);
    if ((size_t)pe_offset + 24u > image_bytes->size) {
        return bad_image();
    }

    if (data[pe_offset] != 'P' || data[pe_offset + 1u] != 'E' || data[pe_offset + 2u] != 0 || data[pe_offset + 3u] != 0) {
        return bad_image();
    }

    coff_offset = pe_offset + 4u;
    section_count = read_u16(data + coff_offset + 2u);
    optional_size = read_u16(data + coff_offset + 16u);
    optional_offset = coff_offset + 20u;

    if ((size_t)optional_offset + optional_size > image_bytes->size || section_count > ZACLR_PE_MAX_SECTIONS) {
        return bad_image();
    }

    optional_magic = read_u16(data + optional_offset);
    if (optional_magic != 0x10Bu && optional_magic != 0x20Bu) {
        return bad_image();
    }

    data_directory_offset = optional_offset + (optional_magic == 0x20Bu ? 112u : 96u);
    if ((size_t)data_directory_offset + (15u * 8u) + 8u > image_bytes->size) {
        return bad_image();
    }

    image->pe_header_offset = pe_offset;
    image->optional_header_magic = optional_magic;
    image->section_count = section_count;

    for (index = 0u; index < section_count; ++index) {
        const uint8_t* section = data + optional_offset + optional_size + index * 40u;
        if ((size_t)(section - data) + 40u > image_bytes->size) {
            return bad_image();
        }

        image->sections[index].virtual_size = read_u32(section + 8u);
        image->sections[index].virtual_address = read_u32(section + 12u);
        image->sections[index].raw_size = read_u32(section + 16u);
        image->sections[index].raw_offset = read_u32(section + 20u);
    }

    image->cli_header_rva = read_u32(data + data_directory_offset + 14u * 8u);
    cli_size = read_u32(data + data_directory_offset + 14u * 8u + 4u);
    image->cli_header_size = cli_size;
    if (image->cli_header_rva == 0u) {
        return bad_image();
    }

    result = zaclr_pe_image_rva_to_offset(image, image->cli_header_rva, &cli_offset);
    if (result.status != ZACLR_STATUS_OK || (size_t)cli_offset + 24u > image_bytes->size) {
        return bad_image();
    }

    image->metadata_rva = read_u32(data + cli_offset + 8u);
    metadata_size = read_u32(data + cli_offset + 12u);
    image->metadata_size = metadata_size;
    image->entry_point_token = read_u32(data + cli_offset + 20u);

    result = zaclr_pe_image_rva_to_offset(image, image->metadata_rva, &metadata_offset);
    if (result.status != ZACLR_STATUS_OK || (size_t)metadata_offset + metadata_size > image_bytes->size) {
        return bad_image();
    }

    image->metadata_root.data = data + metadata_offset;
    image->metadata_root.size = metadata_size;

    result = zaclr_metadata_reader_initialize(&metadata_reader, &image->metadata_root);
    if (result.status != ZACLR_STATUS_OK) {
        return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_LOADER);
    }

    result = zaclr_metadata_reader_get_assembly_name(&metadata_reader, &image->assembly_name);
    if (result.status != ZACLR_STATUS_OK) {
        return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_LOADER);
    }

    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_pe_image_rva_to_offset(const struct zaclr_pe_image* image,
                                                             uint32_t rva,
                                                             uint32_t* out_offset)
{
    uint32_t index;

    if (image == NULL || out_offset == NULL) {
        return invalid_argument();
    }

    for (index = 0u; index < image->section_count; ++index) {
        const struct zaclr_pe_section* section = &image->sections[index];
        uint32_t span = section->virtual_size > section->raw_size ? section->virtual_size : section->raw_size;
        if (rva >= section->virtual_address && rva < (section->virtual_address + span)) {
            *out_offset = section->raw_offset + (rva - section->virtual_address);
            if (*out_offset > image->image.size) {
                return bad_image();
            }

            return zaclr_result_ok();
        }
    }

    return bad_image();
}

extern "C" uint32_t zaclr_pe_image_flags(const struct zaclr_pe_image* image)
{
    return image != NULL ? image->flags : 0u;
}
