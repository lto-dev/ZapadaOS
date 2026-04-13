#ifndef ZACLR_ASSEMBLY_SOURCE_H
#define ZACLR_ASSEMBLY_SOURCE_H

#include <kernel/zaclr/include/zaclr_contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_assembly_identity {
    const char* name;
    uint32_t    name_length;
    uint16_t    version_major;
    uint16_t    version_minor;
    uint16_t    version_build;
    uint16_t    version_revision;
    uint8_t     public_key_token[8];
    bool        has_public_key_token;
};

struct zaclr_assembly_image {
    const uint8_t* data;
    size_t         size;
    const char*    source_label;
    bool           caller_owns;
};

typedef struct zaclr_result (*zaclr_source_locate_fn)(void* context,
                                                      const struct zaclr_assembly_identity* identity,
                                                      struct zaclr_assembly_image* out_image);
typedef void (*zaclr_source_release_fn)(void* context, struct zaclr_assembly_image* image);

struct zaclr_assembly_source {
    const char*             name;
    zaclr_source_locate_fn  locate;
    zaclr_source_release_fn release;
    void*                   context;
};

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_ASSEMBLY_SOURCE_H */
