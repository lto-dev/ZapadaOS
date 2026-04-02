#ifndef ZACLR_METADATA_READER_H
#define ZACLR_METADATA_READER_H

#include <kernel/zaclr/metadata/zaclr_type_map.h>

#define ZACLR_METADATA_MAX_TABLES 64u

struct zaclr_metadata_table_view {
    const uint8_t* rows;
    uint32_t row_size;
    uint32_t row_count;
};

struct zaclr_assembly_row {
    uint16_t major_version;
    uint16_t minor_version;
    uint16_t build_number;
    uint16_t revision_number;
    uint32_t flags;
    uint32_t public_key_blob_index;
    uint32_t name_index;
    uint32_t culture_index;
};

struct zaclr_typedef_row {
    uint32_t flags;
    uint32_t name_index;
    uint32_t namespace_index;
    uint32_t extends;
    uint32_t field_list;
    uint32_t method_list;
};

struct zaclr_methoddef_row {
    uint32_t rva;
    uint16_t impl_flags;
    uint16_t flags;
    uint32_t name_index;
    uint32_t signature_blob_index;
    uint32_t param_list;
};

struct zaclr_field_row {
    uint16_t flags;
    uint32_t name_index;
    uint32_t signature_blob_index;
};

struct zaclr_assemblyref_row {
    uint16_t major_version;
    uint16_t minor_version;
    uint16_t build_number;
    uint16_t revision_number;
    uint32_t flags;
    uint32_t public_key_or_token_blob_index;
    uint32_t name_index;
    uint32_t culture_index;
    uint32_t hash_value_blob_index;
};

struct zaclr_exportedtype_row {
    uint32_t flags;
    uint32_t type_def_id;
    uint32_t name_index;
    uint32_t namespace_index;
    uint32_t implementation_coded_index;
};

struct zaclr_standalonesig_row {
    uint32_t signature_blob_index;
};

struct zaclr_moduleref_row {
    uint32_t name_index;
};

struct zaclr_implmap_row {
    uint16_t flags;
    uint32_t member_forwarded_coded_index;
    uint32_t import_name_index;
    uint32_t import_scope;
};

struct zaclr_methodspec_row {
    uint32_t method_coded_index;
    uint32_t instantiation_blob_index;
};

#ifdef __cplusplus
extern "C" {
#endif

struct zaclr_metadata_reader {
    struct zaclr_slice metadata_root;
    struct zaclr_slice strings_heap;
    struct zaclr_slice blob_heap;
    struct zaclr_slice user_string_heap;
    struct zaclr_slice tilde_stream;
    uint8_t heap_sizes;
    uint64_t valid_mask;
    struct zaclr_metadata_table_view tables[ZACLR_METADATA_MAX_TABLES];
    uint32_t row_counts[ZACLR_METADATA_MAX_TABLES];
    uint32_t assembly_name_index;
    uint32_t flags;
};

struct zaclr_result zaclr_metadata_reader_initialize(struct zaclr_metadata_reader* reader,
                                                     const struct zaclr_slice* metadata_root);
struct zaclr_result zaclr_metadata_reader_get_string(const struct zaclr_metadata_reader* reader,
                                                     uint32_t index,
                                                     struct zaclr_name_view* value);
struct zaclr_result zaclr_metadata_reader_get_blob(const struct zaclr_metadata_reader* reader,
                                                   uint32_t index,
                                                   struct zaclr_slice* value);
uint32_t zaclr_metadata_reader_get_row_count(const struct zaclr_metadata_reader* reader,
                                             uint32_t table_id);
struct zaclr_result zaclr_metadata_reader_get_assembly_row(const struct zaclr_metadata_reader* reader,
                                                           uint32_t row_1based,
                                                           struct zaclr_assembly_row* row);
struct zaclr_result zaclr_metadata_reader_get_typedef_row(const struct zaclr_metadata_reader* reader,
                                                          uint32_t row_1based,
                                                          struct zaclr_typedef_row* row);
struct zaclr_result zaclr_metadata_reader_get_field_row(const struct zaclr_metadata_reader* reader,
                                                        uint32_t row_1based,
                                                        struct zaclr_field_row* row);
struct zaclr_result zaclr_metadata_reader_get_methoddef_row(const struct zaclr_metadata_reader* reader,
                                                            uint32_t row_1based,
                                                            struct zaclr_methoddef_row* row);
struct zaclr_result zaclr_metadata_reader_get_assemblyref_row(const struct zaclr_metadata_reader* reader,
                                                              uint32_t row_1based,
                                                              struct zaclr_assemblyref_row* row);
struct zaclr_result zaclr_metadata_reader_get_exportedtype_row(const struct zaclr_metadata_reader* reader,
                                                               uint32_t row_1based,
                                                               struct zaclr_exportedtype_row* row);
struct zaclr_result zaclr_metadata_reader_get_standalonesig_row(const struct zaclr_metadata_reader* reader,
                                                                uint32_t row_1based,
                                                                struct zaclr_standalonesig_row* row);
struct zaclr_result zaclr_metadata_reader_get_moduleref_row(const struct zaclr_metadata_reader* reader,
                                                            uint32_t row_1based,
                                                            struct zaclr_moduleref_row* row);
struct zaclr_result zaclr_metadata_reader_get_implmap_row(const struct zaclr_metadata_reader* reader,
                                                          uint32_t row_1based,
                                                          struct zaclr_implmap_row* row);
struct zaclr_result zaclr_metadata_reader_get_methodspec_row(const struct zaclr_metadata_reader* reader,
                                                             uint32_t row_1based,
                                                             struct zaclr_methodspec_row* row);
struct zaclr_result zaclr_metadata_reader_get_assembly_name(const struct zaclr_metadata_reader* reader,
                                                            struct zaclr_name_view* assembly_name);

#ifdef __cplusplus
}
#endif

#endif /* ZACLR_METADATA_READER_H */
