#ifndef ZACLR_PE_IMAGE_H
#define ZACLR_PE_IMAGE_H

#include <kernel/zaclr/include/zaclr_public_api.h>

#define ZACLR_PE_MAX_SECTIONS 32u

struct zaclr_pe_section {
    uint32_t virtual_address;
    uint32_t virtual_size;
    uint32_t raw_offset;
    uint32_t raw_size;
};

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_pe_image {
    struct zaclr_slice image;
    struct zaclr_name_view assembly_name;
    struct zaclr_slice metadata_root;
    uint32_t cli_header_rva;
    uint32_t cli_header_size;
    uint32_t metadata_rva;
    uint32_t metadata_size;
    uint32_t entry_point_token;
    uint32_t pe_header_offset;
    uint16_t optional_header_magic;
    uint16_t section_count;
    struct zaclr_pe_section sections[ZACLR_PE_MAX_SECTIONS];
    uint32_t flags;
};

struct zaclr_result zaclr_pe_image_parse(const struct zaclr_slice* image_bytes,
                                         struct zaclr_pe_image* image);
struct zaclr_result zaclr_pe_image_rva_to_offset(const struct zaclr_pe_image* image,
                                                 uint32_t rva,
                                                 uint32_t* out_offset);
uint32_t zaclr_pe_image_flags(const struct zaclr_pe_image* image);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_PE_IMAGE_H */
